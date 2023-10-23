#include "sorted_dynamic_store.h"

#include "tablet.h"
#include "transaction.h"
#include "automaton.h"

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_options.h>
#include <yt/yt/ytlib/chunk_client/chunk_writer.h>
#include <yt/yt/ytlib/chunk_client/data_slice_descriptor.h>
#include <yt/yt/ytlib/chunk_client/memory_reader.h>
#include <yt/yt/ytlib/chunk_client/memory_writer.h>

#include <yt/yt/ytlib/table_client/cached_versioned_chunk_meta.h>
#include <yt/yt/ytlib/table_client/chunk_state.h>
#include <yt/yt/ytlib/table_client/performance_counters.h>
#include <yt/yt/ytlib/table_client/versioned_chunk_reader.h>
#include <yt/yt/ytlib/table_client/versioned_chunk_writer.h>

#include <yt/yt/ytlib/tablet_client/config.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/client/table_client/name_table.h>
#include <yt/yt/client/table_client/row_batch.h>
#include <yt/yt/client/table_client/versioned_reader.h>
#include <yt/yt/client/table_client/versioned_row.h>
#include <yt/yt/client/table_client/versioned_writer.h>

#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt/core/misc/linear_probe.h>
#include <yt/yt/core/misc/skip_list.h>

#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/ytree/fluent.h>

#include <library/cpp/yt/small_containers/compact_vector.h>

namespace NYT::NTabletNode {

using namespace NApi;
using namespace NChunkClient::NProto;
using namespace NChunkClient;
using namespace NConcurrency;
using namespace NNodeTrackerClient;
using namespace NObjectClient;
using namespace NTableClient;
using namespace NTransactionClient;
using namespace NYTree;
using namespace NYson;
using namespace NHydra;

using NChunkClient::TDataSliceDescriptor;
using NYT::TRange;

////////////////////////////////////////////////////////////////////////////////

static const size_t ReaderPoolSize = 16_KB;
static const int SnapshotRowsPerRead = 1024;

struct TSortedDynamicStoreReaderPoolTag
{ };

////////////////////////////////////////////////////////////////////////////////

namespace {

ui32 ExtractRevision(ui32 revision)
{
    return revision;
}

ui32 ExtractRevision(const TDynamicValue& value)
{
    return value.Revision;
}

template <class T>
bool AllocateListForPushIfNeeded(
    TEditList<T>* list,
    TChunkedMemoryPool* pool)
{
    if (*list) {
        if (list->GetSize() < list->GetCapacity()) {
            return false;
        }
    }

    int newCapacity = std::min(
        *list ? list->GetCapacity() * EditListCapacityMultiplier : InitialEditListCapacity,
        MaxEditListCapacity);
    auto newList = TEditList<T>::Allocate(pool, newCapacity);

    if (*list) {
        newList.SetSuccessor(*list);
    }

    *list = newList;
    return true;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

class TSortedDynamicStore::TLookupHashTable
{
public:
    TLookupHashTable(
        int size,
        TSortedDynamicRowKeyComparer keyComparer,
        int keyColumnCount)
        : HashTable_(size)
        , KeyComparer_(std::move(keyComparer))
        , KeyColumnCount_(keyColumnCount)
        , Size_(size)
    { }

    void Insert(const TUnversionedValue* keyBegin, TSortedDynamicRow dynamicRow)
    {
        auto fingerprint = GetFarmFingerprint(MakeRange(keyBegin, KeyColumnCount_));
        auto value = reinterpret_cast<ui64>(dynamicRow.GetHeader());
        YT_VERIFY(HashTable_.Insert(fingerprint, value));
    }

    void Insert(TUnversionedRow row, TSortedDynamicRow dynamicRow)
    {
        Insert(row.Begin(), dynamicRow);
    }

    TSortedDynamicRow Find(TLegacyKey key) const
    {
        auto fingerprint = GetFarmFingerprint(key);
        TCompactVector<ui64, 1> items;
        HashTable_.Find(fingerprint, &items);
        for (auto item : items) {
            auto dynamicRow = TSortedDynamicRow(reinterpret_cast<TSortedDynamicRowHeader*>(item));
            if (KeyComparer_(dynamicRow, ToKeyRef(key)) == 0) {
                return dynamicRow;
            }
        }
        return TSortedDynamicRow();
    }

    size_t GetByteSize() const
    {
        return HashTable_.GetByteSize();
    }

    int GetSize() const
    {
        return Size_;
    }

private:
    TLinearProbeHashTable HashTable_;
    const TSortedDynamicRowKeyComparer KeyComparer_;
    const int KeyColumnCount_;
    const int Size_;
};

////////////////////////////////////////////////////////////////////////////////

class TSortedDynamicStore::TReaderBase
{
public:
    TReaderBase(
        TSortedDynamicStorePtr store,
        TTabletSnapshotPtr tabletSnapshot,
        TTimestamp timestamp,
        bool produceAllVersions,
        ui32 revision,
        const TColumnFilter& columnFilter)
        : Store_(std::move(store))
        , TabletSnapshot_(std::move(tabletSnapshot))
        , Timestamp_(timestamp)
        , ProduceAllVersions_(produceAllVersions)
        , Revision_(revision)
        , ColumnFilter_(columnFilter)
        , KeyColumnCount_(Store_->KeyColumnCount_)
        , SchemaColumnCount_(Store_->SchemaColumnCount_)
        , ColumnLockCount_(Store_->ColumnLockCount_)
        , Pool_(TSortedDynamicStoreReaderPoolTag(), ReaderPoolSize)
    {
        YT_VERIFY(Timestamp_ != AllCommittedTimestamp || ColumnFilter_.IsUniversal());

        LockMask_.Set(PrimaryLockIndex, ELockType::SharedWeak);
        if (columnFilter.IsUniversal()) {
            LockMask_.Enrich(ColumnLockCount_);
        } else {
            for (int columnIndex : columnFilter.GetIndexes()) {
                if (columnIndex < Store_->KeyColumnCount_) {
                    // Key columns don't have corresponding locks.
                    continue;
                }
                int lockIndex = Store_->ColumnIndexToLockIndex_[columnIndex];
                LockMask_.Set(lockIndex, ELockType::SharedWeak);
            }
        }
    }

protected:
    const TSortedDynamicStorePtr Store_;
    const TTabletSnapshotPtr TabletSnapshot_;
    const TTimestamp Timestamp_;
    const bool ProduceAllVersions_;
    const ui32 Revision_;
    const TColumnFilter ColumnFilter_;

    int KeyColumnCount_;
    int SchemaColumnCount_;
    int ColumnLockCount_;

    TChunkedMemoryPool Pool_;

    std::vector<TTimestamp> DeleteTimestamps_;
    std::vector<TTimestamp> WriteTimestamps_;
    std::vector<TVersionedValue> VersionedValues_;

    TLockMask LockMask_;

    TTimestamp FillLatestWriteTimestamps(TSortedDynamicRow dynamicRow, TTimestamp* latestWriteTimestampPerLock)
    {
        auto* lock = dynamicRow.BeginLocks(KeyColumnCount_);
        auto maxTimestamp = NullTimestamp;
        for (int index = 0; index < ColumnLockCount_; ++index, ++lock) {
            auto list = TSortedDynamicRow::GetWriteRevisionList(*lock);
            const auto* revisionPtr = SearchByTimestamp(list, Timestamp_);

            auto timestamp = revisionPtr
                ? Store_->TimestampFromRevision(*revisionPtr)
                : NullTimestamp;

            latestWriteTimestampPerLock[index] = timestamp;
            maxTimestamp = std::max(maxTimestamp, timestamp);
        }

        auto primaryLockTimestamp = latestWriteTimestampPerLock[PrimaryLockIndex];
        for (int index = PrimaryLockIndex + 1; index < ColumnLockCount_; ++index) {
            if (latestWriteTimestampPerLock[index] < primaryLockTimestamp) {
                latestWriteTimestampPerLock[index] = primaryLockTimestamp;
            }
        }

        return maxTimestamp;
    }

    TTimestamp GetLatestDeleteTimestamp(TSortedDynamicRow dynamicRow)
    {
        auto list = dynamicRow.GetDeleteRevisionList(KeyColumnCount_, ColumnLockCount_);
        const auto* revisionPtr = SearchByTimestamp(list, Timestamp_);
        return revisionPtr
            ? Store_->TimestampFromRevision(*revisionPtr)
            : NullTimestamp;
    }

    TVersionedRow ProduceSingleRowVersion(TSortedDynamicRow dynamicRow)
    {
        Store_->WaitOnBlockedRow(dynamicRow, LockMask_, Timestamp_);

        // Prepare timestamps.
        std::array<TTimestamp, MaxColumnLockCount> latestWriteTimestampPerLock;
        auto latestWriteTimestamp = FillLatestWriteTimestamps(dynamicRow, latestWriteTimestampPerLock.data());
        auto latestDeleteTimestamp = GetLatestDeleteTimestamp(dynamicRow);

        if (latestWriteTimestamp == NullTimestamp && latestDeleteTimestamp == NullTimestamp) {
            return TVersionedRow();
        }

        int writeTimestampCount = 1;
        int deleteTimestampCount = 1;

        if (latestDeleteTimestamp == NullTimestamp) {
            deleteTimestampCount = 0;
        } else if (latestDeleteTimestamp > latestWriteTimestamp) {
            writeTimestampCount = 0;
        }

        // Prepare values.
        VersionedValues_.clear();

        const auto& schemaColumns = TabletSnapshot_->PhysicalSchema->Columns();

        auto fillValue = [&] (int index) {
            // NB: Inserting a new item into value list and adding a new write revision cannot
            // be done atomically. We always append values before revisions but in the middle of these
            // two steps there might be "phantom" values present in the row.
            // To work this around, we cap the value lists by #latestWriteTimestamp to make sure that
            // no "phantom" value is listed.

            int lockIndex = Store_->ColumnIndexToLockIndex_[index];
            auto latestWriteTimestamp = latestWriteTimestampPerLock[lockIndex];

            auto list = dynamicRow.GetFixedValueList(index, KeyColumnCount_, ColumnLockCount_);
            if (schemaColumns[index].Aggregate()) {
                ExtractByTimestamp(
                    list,
                    latestDeleteTimestamp,
                    latestWriteTimestamp,
                    [&] (const TDynamicValue& value) {
                        ProduceVersionedValue(&VersionedValues_.emplace_back(), index, value);
                    });
            } else {
                const auto* value = SearchByTimestamp(list, latestWriteTimestamp);
                if (value && Store_->TimestampFromRevision(value->Revision) > latestDeleteTimestamp) {
                    ProduceVersionedValue(&VersionedValues_.emplace_back(), index, *value);
                }
            }
        };

        if (ColumnFilter_.IsUniversal()) {
            for (int index = KeyColumnCount_; index < SchemaColumnCount_; ++index) {
                fillValue(index);
            }
        } else {
            for (int index : ColumnFilter_.GetIndexes()) {
                if (index >= KeyColumnCount_) {
                    fillValue(index);
                }
            }
        }

        auto versionedRow = TMutableVersionedRow::Allocate(
            &Pool_,
            KeyColumnCount_,
            VersionedValues_.size(),
            writeTimestampCount,
            deleteTimestampCount);

        // Keys.
        ProduceKeys(dynamicRow, versionedRow.BeginKeys());

        // Timestamps.
        if (writeTimestampCount > 0) {
            versionedRow.WriteTimestamps()[0] = latestWriteTimestamp;
        }
        if (deleteTimestampCount > 0) {
            versionedRow.DeleteTimestamps()[0] = latestDeleteTimestamp;
        }

        // Values.
        ::memcpy(versionedRow.BeginValues(), VersionedValues_.data(), sizeof (TVersionedValue) * VersionedValues_.size());

        return versionedRow;
    }

    TVersionedRow ProduceAllRowVersions(TSortedDynamicRow dynamicRow, bool snapshotMode)
    {
        Store_->WaitOnBlockedRow(dynamicRow, LockMask_, Timestamp_);

        std::array<TTimestamp, MaxColumnLockCount> latestWriteTimestampPerLock;
        FillLatestWriteTimestamps(dynamicRow, latestWriteTimestampPerLock.data());

        // Prepare values and write timestamps.
        VersionedValues_.clear();
        WriteTimestamps_.clear();
        for (int columnIndex = KeyColumnCount_; columnIndex < SchemaColumnCount_; ++columnIndex) {
            int lockIndex = Store_->ColumnIndexToLockIndex_[columnIndex];
            auto latestWriteTimestamp = latestWriteTimestampPerLock[lockIndex];
            auto list = dynamicRow.GetFixedValueList(columnIndex, KeyColumnCount_, ColumnLockCount_);

            ExtractByTimestamp(
                list,
                NullTimestamp,
                latestWriteTimestamp,
                [&] (const TDynamicValue& value) {
                    if (value.Revision > Revision_) {
                        return;
                    }

                    auto* versionedValue = &VersionedValues_.emplace_back();
                    ProduceVersionedValue(versionedValue, columnIndex, value);
                    WriteTimestamps_.push_back(versionedValue->Timestamp);
                });
        }
        std::sort(WriteTimestamps_.begin(), WriteTimestamps_.end(), std::greater<TTimestamp>());
        WriteTimestamps_.erase(
            std::unique(WriteTimestamps_.begin(), WriteTimestamps_.end()),
            WriteTimestamps_.end());

        // Prepare delete timestamps.
        DeleteTimestamps_.clear();
        for (auto list = dynamicRow.GetDeleteRevisionList(KeyColumnCount_, ColumnLockCount_);
             list;
             list = list.GetSuccessor())
        {
            for (int itemIndex = UpperBoundByTimestamp(list, Timestamp_) - 1; itemIndex >= 0; --itemIndex) {
                ui32 revision = list[itemIndex];
                if (revision <= Revision_) {
                    DeleteTimestamps_.push_back(Store_->TimestampFromRevision(revision));
                    YT_ASSERT(DeleteTimestamps_.size() == 1 ||
                            DeleteTimestamps_.back() < DeleteTimestamps_[DeleteTimestamps_.size() - 2]);
                }
            }
        }

        auto shouldProduce = !WriteTimestamps_.empty() || !DeleteTimestamps_.empty();
        if (snapshotMode && !shouldProduce) {
            // Row is empty but it may be read locked.
            for (int index = 0; index < ColumnLockCount_; ++index) {
                auto& lock = dynamicRow.BeginLocks(KeyColumnCount_)[index];
                for (auto list = dynamicRow.GetReadLockRevisionList(lock);
                    list;
                    list = list.GetSuccessor())
                {
                    for (int itemIndex = UpperBoundByTimestamp(list, Timestamp_) - 1; itemIndex >= 0; --itemIndex) {
                        auto revision = list[itemIndex];
                        auto timestamp = Store_->TimestampFromRevision(revision);
                        if (revision <= Revision_ && timestamp != NullTimestamp && timestamp != MinTimestamp) {
                            shouldProduce = true;
                            break;
                        }
                    }
                }

                if (shouldProduce) {
                    break;
                }
            }
        }

        // In snapshot mode it means that row is transient (i.e. was not affected by any mutations).
        // We do not store such rows for the sake of determinism.
        // In non-snapshot mode it means that there are no values and delete timestamps in row.
        if (!shouldProduce) {
            return TVersionedRow();
        }

        auto versionedRow = TMutableVersionedRow::Allocate(
            &Pool_,
            KeyColumnCount_,
            VersionedValues_.size(),
            WriteTimestamps_.size(),
            DeleteTimestamps_.size());

        // Keys.
        ProduceKeys(dynamicRow, versionedRow.BeginKeys());

        // Timestamps (sorted in descending order).
        ::memcpy(versionedRow.BeginWriteTimestamps(), WriteTimestamps_.data(), sizeof(TTimestamp) * WriteTimestamps_.size());
        ::memcpy(versionedRow.BeginDeleteTimestamps(), DeleteTimestamps_.data(), sizeof(TTimestamp) * DeleteTimestamps_.size());

        // Values.
        ::memcpy(versionedRow.BeginValues(), VersionedValues_.data(), sizeof(TVersionedValue) * VersionedValues_.size());

        return versionedRow;
    }

    void ProduceKeys(TSortedDynamicRow dynamicRow, TUnversionedValue* dstKey)
    {
        ui32 nullKeyMask = dynamicRow.GetNullKeyMask();
        ui32 nullKeyBit = 1;
        const auto* srcKey = dynamicRow.BeginKeys();
        for (int index = 0;
             index < KeyColumnCount_;
             ++index, nullKeyBit <<= 1, ++srcKey, ++dstKey)
        {
            ProduceUnversionedValue(
                dstKey,
                index,
                *srcKey,
                /*null*/ (nullKeyMask & nullKeyBit) != 0,
                /*flags*/ {});
        }
    }

    void ProduceUnversionedValue(
        TUnversionedValue* dstValue,
        int index,
        TDynamicValueData srcData,
        bool null,
        EValueFlags flags)
    {
        *dstValue = {};
        dstValue->Id = index;
        dstValue->Flags = flags;
        if (null) {
            dstValue->Type = EValueType::Null;
            return;
        }
        dstValue->Type = Store_->Schema_->Columns()[index].GetWireType();
        if (IsStringLikeType(dstValue->Type)) {
            dstValue->Length = srcData.String->Length;
            dstValue->Data.String = srcData.String->Data;
        } else {
            ::memcpy(&dstValue->Data, &srcData, sizeof(TDynamicValueData));
        }
    }

    void ProduceVersionedValue(TVersionedValue* dstValue, int index, const TDynamicValue& srcValue)
    {
        ProduceUnversionedValue(dstValue, index, srcValue.Data, srcValue.Null, srcValue.Flags);
        dstValue->Timestamp = Store_->TimestampFromRevision(srcValue.Revision);
    }


    template <class T>
    T* SearchByTimestamp(TEditList<T> list, TTimestamp maxTimestamp)
    {
        if (maxTimestamp == SyncLastCommittedTimestamp || maxTimestamp == AsyncLastCommittedTimestamp) {
            while (list) {
                int size = list.GetSize();
                if (size > 0) {
                    return &list[size - 1];
                }
                list = list.GetSuccessor();
            }
            return nullptr;
        } else {
            while (list) {
                if (!list.IsEmpty() && Store_->TimestampFromRevision(ExtractRevision(list[0])) <= maxTimestamp) {
                    break;
                }
                list = list.GetSuccessor();
            }

            if (!list) {
                return nullptr;
            }

            YT_ASSERT(!list.IsEmpty());

            auto* value = std::lower_bound(
                list.Begin(),
                list.End(),
                maxTimestamp,
                [&] (const T& element, TTimestamp timestamp) {
                    return Store_->TimestampFromRevision(ExtractRevision(element)) <= timestamp;
                }) - 1;

            YT_ASSERT(value >= list.Begin() || Store_->TimestampFromRevision(ExtractRevision(*value)) <= maxTimestamp);
            return value;
        }
    }

    template<class T>
    int UpperBoundByTimestamp(TEditList<T> list, TTimestamp maxTimestamp)
    {
        if (!list) {
            return 0;
        }

        if (maxTimestamp == SyncLastCommittedTimestamp || maxTimestamp == AsyncLastCommittedTimestamp) {
            return list.GetSize();
        }

        return std::lower_bound(
            list.Begin(),
            list.End(),
            maxTimestamp,
            [&] (const T& element, TTimestamp timestamp) {
                return Store_->TimestampFromRevision(ExtractRevision(element)) <= timestamp;
            }) - list.Begin();
    }

    template <class T, class TValueExtractor>
    void ExtractByTimestamp(
        TEditList<T> list,
        TTimestamp minTimestamp,
        TTimestamp maxTimestamp,
        const TValueExtractor& valueExtractor)
    {
        for (; list; list = list.GetSuccessor()) {
            if (list.GetSize() == 0) {
                // Skip empty list.
                continue;
            }

            if (Store_->TimestampFromRevision(ExtractRevision(list[0])) > maxTimestamp) {
                // Skip list since all of its timestamps are greater than maxTimestamp.
                continue;
            }

            const auto* begin = list.Begin();
            const auto* end = list.End();
            if (Store_->TimestampFromRevision(ExtractRevision(*(end - 1))) > maxTimestamp) {
                // Adjust end to skip all timestamps that are greater than maxTimestamp.
                end = std::lower_bound(
                    begin,
                    end,
                    maxTimestamp,
                    [&] (const T& element, TTimestamp value) {
                        return Store_->TimestampFromRevision(ExtractRevision(element)) <= value;
                    });
            }

            for (const auto* current = end - 1; current >= begin; --current) {
                if (Store_->TimestampFromRevision(ExtractRevision(*current)) < minTimestamp) {
                    // Interrupt upon reaching the first timestamp that is less than minTimestamp.
                    return;
                }
                valueExtractor(*current);
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

class TSortedDynamicStore::TRangeReader
    : public TReaderBase
    , public IVersionedReader
{
public:
    TRangeReader(
        TSortedDynamicStorePtr store,
        TTabletSnapshotPtr tabletSnapshot,
        TSharedRange<TRowRange> ranges,
        TTimestamp timestamp,
        bool produceAllVersions,
        bool snapshotMode,
        ui32 revision,
        const TColumnFilter& columnFilter)
        : TReaderBase(
            std::move(store),
            std::move(tabletSnapshot),
            timestamp,
            produceAllVersions,
            revision,
            columnFilter)
        , Ranges_(std::move(ranges))
        , SnapshotMode_(snapshotMode)
    {
        YT_VERIFY(!SnapshotMode_ || ProduceAllVersions_);
    }

    TFuture<void> Open() override
    {
        UpdateLimits();
        return VoidFuture;
    }

    IVersionedRowBatchPtr Read(const TRowBatchReadOptions& options) override
    {
        YT_ASSERT(options.MaxRowsPerRead > 0);
        std::vector<TVersionedRow> rows;
        rows.reserve(options.MaxRowsPerRead);
        Pool_.Clear();

        if (!Iterator_.IsValid()) {
            return nullptr;
        }

        const auto& keyComparer = Store_->GetRowKeyComparer();

        while (Iterator_.IsValid() && rows.size() < rows.capacity()) {
            if (keyComparer(Iterator_.GetCurrent(), ToKeyRef(UpperBound_)) >= 0) {
                UpdateLimits();
                if (!Iterator_.IsValid()) {
                    break;
                }
            }

            auto row = ProduceRow(Iterator_.GetCurrent());
            if (row) {
                rows.push_back(row);
                DataWeight_ += GetDataWeight(row);
            }

            Iterator_.MoveNext();
        }

        i64 rowCount = rows.size();

        RowCount_ += rowCount;

        return CreateBatchFromVersionedRows(MakeSharedRange(std::move(rows), MakeStrong(this)));
    }

    TFuture<void> GetReadyEvent() const override
    {
        return VoidFuture;
    }

    TDataStatistics GetDataStatistics() const override
    {
        TDataStatistics dataStatistics;
        dataStatistics.set_row_count(RowCount_);
        dataStatistics.set_data_weight(DataWeight_);
        return dataStatistics;
    }

    TCodecStatistics GetDecompressionStatistics() const override
    {
        return TCodecStatistics();
    }

    bool IsFetchingCompleted() const override
    {
        return true;
    }

    std::vector<TChunkId> GetFailedChunkIds() const override
    {
        return std::vector<TChunkId>();
    }

private:
    const TSharedRange<TRowRange> Ranges_;
    const bool SnapshotMode_;

    TLegacyKey LowerBound_;
    TLegacyKey UpperBound_;
    size_t RangeIndex_ = 0;
    i64 RowCount_  = 0;
    i64 DataWeight_ = 0;

    using TIterator = TSkipList<TSortedDynamicRow, TSortedDynamicRowKeyComparer>::TIterator;
    TIterator Iterator_;

    void UpdateLimits()
    {
        const auto& keyComparer = Store_->GetRowKeyComparer();

        while (RangeIndex_ < Ranges_.Size()) {
            LowerBound_ = Ranges_[RangeIndex_].first;
            UpperBound_ = Ranges_[RangeIndex_].second;

            Iterator_ = Store_->Rows_->FindGreaterThanOrEqualTo(ToKeyRef(LowerBound_));

            if (Iterator_.IsValid() && keyComparer(Iterator_.GetCurrent(), ToKeyRef(UpperBound_)) >= 0) {
                auto newBoundIt = std::upper_bound(
                    Ranges_.begin() + RangeIndex_,
                    Ranges_.end(),
                    Iterator_.GetCurrent(),
                    [&] (const TSortedDynamicRow& lhs, const TRowRange& rhs) {
                        return keyComparer(lhs, ToKeyRef(rhs.second)) < 0;
                    });

                RangeIndex_ = std::distance(Ranges_.begin(), newBoundIt);
                continue;
            }

            ++RangeIndex_;
            return;
        }
        Iterator_ = TIterator();
    }

    TVersionedRow ProduceRow(const TSortedDynamicRow& dynamicRow)
    {
        return ProduceAllVersions_
            ? ProduceAllRowVersions(dynamicRow, SnapshotMode_)
            : ProduceSingleRowVersion(dynamicRow);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TSortedDynamicStore::TLookupReader
    : public TReaderBase
    , public IVersionedReader
{
public:
    TLookupReader(
        TSortedDynamicStorePtr store,
        TTabletSnapshotPtr tabletSnapshot,
        TSharedRange<TLegacyKey> keys,
        TTimestamp timestamp,
        bool produceAllVersions,
        const TColumnFilter& columnFilter)
        : TReaderBase(
            std::move(store),
            std::move(tabletSnapshot),
            timestamp,
            produceAllVersions,
            MaxRevision,
            columnFilter)
        , Keys_(std::move(keys))
    { }

    TFuture<void> Open() override
    {
        return VoidFuture;
    }

    TFuture<void> GetReadyEvent() const override
    {
        return VoidFuture;
    }

    IVersionedRowBatchPtr Read(const TRowBatchReadOptions& options) override
    {
        YT_ASSERT(options.MaxRowsPerRead > 0);
        std::vector<TVersionedRow> rows;
        rows.reserve(
            std::min(
                std::ssize(Keys_) - RowCount_,
                options.MaxRowsPerRead));
        Pool_.Clear();

        if (Finished_) {
            return nullptr;
        }

        while (rows.size() < rows.capacity()) {
            YT_VERIFY(RowCount_ < std::ssize(Keys_));

            TVersionedRow row;
            if (Y_LIKELY(Store_->LookupHashTable_)) {
                auto dynamicRow = Store_->LookupHashTable_->Find(Keys_[RowCount_]);
                if (dynamicRow) {
                    row = ProduceRow(dynamicRow);
                }
            } else {
                auto iterator = Store_->Rows_->FindEqualTo(ToKeyRef(Keys_[RowCount_]));
                if (iterator.IsValid()) {
                    row = ProduceRow(iterator.GetCurrent());
                }
            }
            rows.push_back(row);
            ++RowCount_;
            ExistingRowCount_ += static_cast<bool>(row);
            DataWeight_ += GetDataWeight(row);
        }

        if (rows.empty()) {
            Finished_ = true;
            return nullptr;
        }

        return CreateBatchFromVersionedRows(MakeSharedRange(std::move(rows), MakeStrong(this)));
    }

    TDataStatistics GetDataStatistics() const override
    {
        TDataStatistics dataStatistics;
        dataStatistics.set_row_count(ExistingRowCount_);
        dataStatistics.set_data_weight(DataWeight_);
        return dataStatistics;
    }

    TCodecStatistics GetDecompressionStatistics() const override
    {
        return TCodecStatistics();
    }

    bool IsFetchingCompleted() const override
    {
        return true;
    }

    std::vector<TChunkId> GetFailedChunkIds() const override
    {
        return std::vector<TChunkId>();
    }

private:
    const TSharedRange<TLegacyKey> Keys_;

    i64 RowCount_  = 0;
    i64 ExistingRowCount_ = 0;
    i64 DataWeight_ = 0;
    bool Finished_ = false;

    TVersionedRow ProduceRow(const TSortedDynamicRow& dynamicRow)
    {
        return ProduceAllVersions_
            ? ProduceAllRowVersions(dynamicRow, /*snapshotMode*/ false)
            : ProduceSingleRowVersion(dynamicRow);
    }
};

////////////////////////////////////////////////////////////////////////////////

TSortedDynamicStore::TSortedDynamicStore(
    TTabletManagerConfigPtr config,
    TStoreId id,
    TTablet* tablet)
    : TDynamicStoreBase(config, id, tablet)
    , RowKeyComparer_(tablet->GetRowKeyComparer())
    , Rows_(new TSkipList<TSortedDynamicRow, TSortedDynamicRowKeyComparer>(
        RowBuffer_->GetPool(),
        RowKeyComparer_))
{
    // Reserve the vector to prevent reallocations and thus enable accessing
    // it from arbitrary threads.
    RevisionToTimestamp_.ReserveChunks(MaxRevisionChunks);
    RevisionToTimestamp_.PushBack(NullTimestamp);
    RevisionToTimestamp_[NullRevision] = NullTimestamp;

    if (Tablet_->GetHashTableSize() > 0) {
        LookupHashTable_ = std::make_unique<TLookupHashTable>(
            Tablet_->GetHashTableSize(),
            RowKeyComparer_,
            Tablet_->GetPhysicalSchema()->GetKeyColumnCount());
    }

    YT_LOG_DEBUG("Sorted dynamic store created (LookupHashTable: %v)",
        static_cast<bool>(LookupHashTable_));
}

TSortedDynamicStore::~TSortedDynamicStore() = default;

IVersionedReaderPtr TSortedDynamicStore::CreateFlushReader()
{
    YT_VERIFY(FlushRevision_ != InvalidRevision);
    return New<TRangeReader>(
        this,
        nullptr,
        MakeSingletonRowRange(MinKey(), MaxKey()),
        AllCommittedTimestamp,
        /*produceAllVersions*/ true,
        /*snapshotMode*/ false,
        FlushRevision_,
        TColumnFilter());
}

IVersionedReaderPtr TSortedDynamicStore::CreateSnapshotReader()
{
    return New<TRangeReader>(
        this,
        nullptr,
        MakeSingletonRowRange(MinKey(), MaxKey()),
        AllCommittedTimestamp,
        /*produceAllVersions*/ true,
        /*snapshotMode*/ true,
        GetLatestRevision(),
        TColumnFilter());
}

const TSortedDynamicRowKeyComparer& TSortedDynamicStore::GetRowKeyComparer() const
{
    return RowKeyComparer_;
}

void TSortedDynamicStore::SetRowBlockedHandler(TRowBlockedHandler handler)
{
    auto guard = WriterGuard(RowBlockedLock_);
    RowBlockedHandler_ = std::move(handler);
}

void TSortedDynamicStore::ResetRowBlockedHandler()
{
    auto guard = WriterGuard(RowBlockedLock_);
    RowBlockedHandler_.Reset();
}

void TSortedDynamicStore::WaitOnBlockedRow(
    TSortedDynamicRow row,
    TLockMask lockMask,
    TTimestamp timestamp)
{
    if (timestamp == AsyncLastCommittedTimestamp ||
        timestamp == AllCommittedTimestamp ||
        Atomicity_ == EAtomicity::None)
    {
        return;
    }

    auto now = NProfiling::GetCpuInstant();
    auto deadline = now + NProfiling::DurationToCpuDuration(Config_->MaxBlockedRowWaitTime);

    while (true) {
        int lockIndex = GetBlockingLockIndex(row, lockMask, timestamp);
        if (lockIndex < 0) {
            break;
        }

        auto throwError = [&] (NTabletClient::EErrorCode errorCode, const TString& message) {
            THROW_ERROR_EXCEPTION(errorCode, message)
                << TErrorAttribute("lock", LockIndexToName_[lockIndex])
                << TErrorAttribute("tablet_id", TabletId_)
                << TErrorAttribute("table_path", TablePath_)
                << TErrorAttribute("key", RowToKey(row))
                << TErrorAttribute("timeout", Config_->MaxBlockedRowWaitTime);
        };

        auto handler = GetRowBlockedHandler();
        if (!handler) {
            throwError(NTabletClient::EErrorCode::RowIsBlocked, "Row is blocked");
        }

        handler.Run(row, lockIndex);

        if (NProfiling::GetCpuInstant() > deadline) {
            throwError(NTabletClient::EErrorCode::BlockedRowWaitTimeout, "Timed out waiting on blocked row");
        }
    }
}

TSortedDynamicRow TSortedDynamicStore::ModifyRow(
    TUnversionedRow row,
    TLockMask lockMask,
    bool isDelete,
    TWriteContext* context)
{
    YT_ASSERT(FlushRevision_ != MaxRevision);

    TSortedDynamicRow result;

    auto commitTimestamp = context->CommitTimestamp;

    ui32 revision = commitTimestamp == NullTimestamp
        ? InvalidRevision
        : RegisterRevision(commitTimestamp);

    auto maybeWriteRow = [&] (TSortedDynamicRow dynamicRow) {
        if (commitTimestamp == NullTimestamp) {
            return;
        }

        WriteRow(dynamicRow, row, revision);
    };

    auto newKeyProvider = [&] () -> TSortedDynamicRow {
        YT_ASSERT(StoreState_ == EStoreState::ActiveDynamic);

        auto dynamicRow = AllocateRow();

        // Copy keys.
        SetKeys(dynamicRow, row.Begin());

        if (context->Phase == EWritePhase::Prelock || context->Phase == EWritePhase::Lock) {
            // Acquire the lock.
            AcquireRowLocks(dynamicRow, lockMask, isDelete, context);
        }

         // Copy values.
        maybeWriteRow(dynamicRow);

        InsertIntoLookupHashTable(row.Begin(), dynamicRow);

        result = dynamicRow;
        return dynamicRow;
    };

    auto existingKeyConsumer = [&] (TSortedDynamicRow dynamicRow) {
        if (context->Phase == EWritePhase::Prelock) {
            // Make sure the row is not blocked.
            if (!CheckRowBlocking(dynamicRow, lockMask, context)) {
                return;
            }

            // Check for lock conflicts and acquire the lock.
            auto error = CheckRowLocks(dynamicRow, context->Transaction, lockMask);
            if (!error.IsOK()) {
                context->Error = error;
                return;
            }
        }

        if (context->Phase == EWritePhase::Prelock || context->Phase == EWritePhase::Lock) {
            // Acquire the lock.
            AcquireRowLocks(dynamicRow, lockMask, isDelete, context);
        }

        // Copy values.
        maybeWriteRow(dynamicRow);

        result = dynamicRow;
    };

    Rows_->Insert(ToKeyRef(row, KeyColumnCount_), newKeyProvider, existingKeyConsumer);

    if (!result) {
        return TSortedDynamicRow();
    }

    if (commitTimestamp != NullTimestamp) {
        if (isDelete) {
            AddDeleteRevision(result, revision);
        } else {
            auto& primaryLock = result.BeginLocks(KeyColumnCount_)[PrimaryLockIndex];
            AddWriteRevision(primaryLock, revision);
        }
        UpdateTimestampRange(commitTimestamp);
    }

    OnDynamicMemoryUsageUpdated();

    auto dataWeight = GetDataWeight(row);
    if (isDelete) {
        ++PerformanceCounters_->DynamicRowDeleteCount;
    } else {
        ++PerformanceCounters_->DynamicRowWriteCount;
    }
    PerformanceCounters_->DynamicRowWriteDataWeight += dataWeight;
    ++context->RowCount;
    context->DataWeight += dataWeight;

    return result;
}

TSortedDynamicRow TSortedDynamicStore::ModifyRow(TVersionedRow row, TWriteContext* context)
{
    YT_ASSERT(FlushRevision_ != MaxRevision);

    auto* timestampToRevision = &context->TimestampToRevision;
    TSortedDynamicRow result;

    auto newKeyProvider = [&] () -> TSortedDynamicRow {
        YT_ASSERT(StoreState_ == EStoreState::ActiveDynamic);

        auto dynamicRow = AllocateRow();

        // Copy keys.
        SetKeys(dynamicRow, row.BeginKeys());

        InsertIntoLookupHashTable(row.BeginKeys(), dynamicRow);

        result = dynamicRow;
        return dynamicRow;
    };

    auto existingKeyConsumer = [&] (TSortedDynamicRow dynamicRow) {
        result = dynamicRow;
    };

    Rows_->Insert(ToKeyRef(row), newKeyProvider, existingKeyConsumer);

    WriteRevisions_.clear();
    for (const auto& value : row.Values()) {
        auto revision = CaptureTimestamp(value.Timestamp, timestampToRevision);
        WriteRevisions_.push_back(revision);

        TDynamicValue dynamicValue;
        CaptureUnversionedValue(&dynamicValue, value);
        dynamicValue.Revision = revision;
        AddValue(result, value.Id, std::move(dynamicValue));
    }

    std::sort(
        WriteRevisions_.begin(),
        WriteRevisions_.end(),
        [&] (ui32 lhs, ui32 rhs) {
            return TimestampFromRevision(lhs) < TimestampFromRevision(rhs);
        });
    WriteRevisions_.erase(std::unique(
        WriteRevisions_.begin(),
        WriteRevisions_.end(),
        [&] (ui32 lhs, ui32 rhs) {
            return TimestampFromRevision(lhs) == TimestampFromRevision(rhs);
        }),
        WriteRevisions_.end());
    auto& primaryLock = result.BeginLocks(KeyColumnCount_)[PrimaryLockIndex];
    for (auto revision : WriteRevisions_) {
        AddWriteRevision(primaryLock, revision);
        UpdateTimestampRange(TimestampFromRevision(revision));
    }

    for (const auto* timestamp = row.EndDeleteTimestamps() - 1; timestamp >= row.BeginDeleteTimestamps(); --timestamp) {
        auto revision = CaptureTimestamp(*timestamp, timestampToRevision);
        AddDeleteRevision(result, revision);
        UpdateTimestampRange(TimestampFromRevision(revision));
    }

    OnDynamicMemoryUsageUpdated();

    auto dataWeight = GetDataWeight(row);
    ++PerformanceCounters_->DynamicRowWriteCount;
    PerformanceCounters_->DynamicRowWriteDataWeight += dataWeight;
    ++context->RowCount;
    context->DataWeight += dataWeight;

    return result;
}

TSortedDynamicRow TSortedDynamicStore::MigrateRow(
    TTransaction* transaction,
    TSortedDynamicRow row,
    TLockMask lockMask)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);
    YT_ASSERT(FlushRevision_ != MaxRevision);

    auto migrateLocks = [&] (TSortedDynamicRow migratedRow) {
        auto* locks = row.BeginLocks(KeyColumnCount_);
        auto* migratedLocks = migratedRow.BeginLocks(KeyColumnCount_);

        // Migrate locks.
        {
            auto* lock = locks;
            auto* lockEnd = locks + ColumnLockCount_;
            auto* migratedLock = migratedLocks;

            for (int index = 0; lock < lockEnd; ++lock, ++migratedLock, ++index) {
                auto lockType = lockMask.Get(index);
                if (lock->WriteTransaction == transaction) {
                    // Write Lock
                    YT_ASSERT(lockType == ELockType::Exclusive);

                    // Validate the original lock's sanity.
                    // NB: For simple commit, transaction may not go through preparation stage
                    // during recovery.
                    YT_ASSERT(
                        transaction->GetPrepareTimestamp() == NullTimestamp ||
                        lock->PrepareTimestamp == transaction->GetPrepareTimestamp());

                    // Validate the migrated lock's sanity.
                    YT_ASSERT(!migratedLock->WriteTransaction);
                    YT_ASSERT(migratedLock->ReadLockCount == 0);
                    YT_ASSERT(migratedLock->PrepareTimestamp == NotPreparedTimestamp);

                    migratedLock->WriteTransaction = lock->WriteTransaction;
                    migratedLock->PrepareTimestamp = lock->PrepareTimestamp.load();
                    if (index == PrimaryLockIndex) {
                        YT_ASSERT(!migratedRow.GetDeleteLockFlag());
                        migratedRow.SetDeleteLockFlag(row.GetDeleteLockFlag());
                    }
                } else if (lockType == ELockType::SharedWeak || lockType == ELockType::SharedStrong) {
                    // Read Lock
                    ++migratedLock->ReadLockCount;
                }
            }
        }

        Lock();
    };

    TSortedDynamicRow result;
    auto newKeyProvider = [&] () -> TSortedDynamicRow {
        // Create migrated row.
        auto migratedRow = result = AllocateRow();

        // Migrate keys.
        SetKeys(migratedRow, row);

        migrateLocks(migratedRow);

        InsertIntoLookupHashTable(RowToKey(row).Begin(), migratedRow);

        return migratedRow;
    };

    auto existingKeyConsumer = [&] (TSortedDynamicRow migratedRow) {
        result = migratedRow;

        migrateLocks(migratedRow);
    };

    Rows_->Insert(
        row,
        newKeyProvider,
        existingKeyConsumer);

    OnDynamicMemoryUsageUpdated();

    return result;
}

void TSortedDynamicStore::PrepareRow(TTransaction* transaction, TSortedDynamicRow row)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);
    YT_ASSERT(FlushRevision_ != MaxRevision);

    auto prepareTimestamp = transaction->GetPrepareTimestamp();
    YT_ASSERT(prepareTimestamp != NullTimestamp);

    {
        auto* lock = row.BeginLocks(KeyColumnCount_);
        for (int index = 0; index < ColumnLockCount_; ++index, ++lock) {
            if (lock->WriteTransaction == transaction) {
                lock->PrepareTimestamp = prepareTimestamp;
            }
        }
    }
}

void TSortedDynamicStore::CommitRow(TTransaction* transaction, TSortedDynamicRow row, TLockMask lockMask)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);
    YT_ASSERT(FlushRevision_ != MaxRevision);

    auto commitTimestamp = transaction->GetCommitTimestamp();
    ui32 commitRevision = RegisterRevision(commitTimestamp);

    auto* lock = row.BeginLocks(KeyColumnCount_);
    auto* lockEnd = lock + ColumnLockCount_;

    for (int index = 0; lock < lockEnd; ++lock, ++index) {
        auto lockType = lockMask.Get(index);
        if (lock->WriteTransaction == transaction) {
            // Write Lock
            YT_ASSERT(lockType == ELockType::Exclusive);
            if (!row.GetDeleteLockFlag()) {
                AddWriteRevision(*lock, commitRevision);
            }
            lock->WriteTransaction = nullptr;
            lock->PrepareTimestamp = NotPreparedTimestamp;
        } else if (lockType == ELockType::SharedWeak) {
            YT_ASSERT(lock->ReadLockCount > 0);
            --lock->ReadLockCount;
        } else if (lockType == ELockType::SharedStrong) {
            YT_ASSERT(lock->ReadLockCount > 0);
            --lock->ReadLockCount;
            AddReadLockRevision(*lock, commitRevision);
        }
    }

    row.SetDeleteLockFlag(false);

    Unlock();

    UpdateTimestampRange(commitTimestamp);
}

void TSortedDynamicStore::AbortRow(TTransaction* transaction, TSortedDynamicRow row, TLockMask lockMask)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);
    YT_ASSERT(FlushRevision_ != MaxRevision);

    auto* lock = row.BeginLocks(KeyColumnCount_);
    auto* lockEnd = lock + ColumnLockCount_;

    for (int index = 0; lock < lockEnd; ++lock, ++index) {
        auto lockType = lockMask.Get(index);
        if (lock->WriteTransaction == transaction) {
            // Write Lock
            YT_ASSERT(lockType == ELockType::Exclusive);
            lock->WriteTransaction = nullptr;
            lock->PrepareTimestamp = NotPreparedTimestamp;
        } else if (lockType == ELockType::SharedWeak || lockType == ELockType::SharedStrong) {
            // Read Lock
            YT_ASSERT(lock->ReadLockCount > 0);
            --lock->ReadLockCount;
        }
    }

    row.SetDeleteLockFlag(false);

    Unlock();
}

void TSortedDynamicStore::DeleteRow(TTransaction* transaction, TSortedDynamicRow dynamicRow)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);
    YT_ASSERT(FlushRevision_ != MaxRevision);

    auto commitTimestamp = transaction->GetCommitTimestamp();
    auto commitRevision = RegisterRevision(commitTimestamp);

    AddDeleteRevision(dynamicRow, commitRevision);
}

void TSortedDynamicStore::WriteRow(TTransaction* transaction, TSortedDynamicRow dynamicRow, TUnversionedRow row)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);
    YT_ASSERT(FlushRevision_ != MaxRevision);

    auto commitTimestamp = transaction->GetCommitTimestamp();
    auto commitRevision = RegisterRevision(commitTimestamp);

    WriteRow(dynamicRow, row, commitRevision);
}

TSortedDynamicRow TSortedDynamicStore::FindRow(TLegacyKey key)
{
    auto it = Rows_->FindEqualTo(ToKeyRef(key));
    return it.IsValid() ? it.GetCurrent() : TSortedDynamicRow();
}

std::vector<TSortedDynamicRow> TSortedDynamicStore::GetAllRows()
{
    std::vector<TSortedDynamicRow> rows;
    for (auto it = Rows_->FindGreaterThanOrEqualTo(ToKeyRef(MinKey()));
         it.IsValid();
         it.MoveNext())
    {
        rows.push_back(it.GetCurrent());
    }
    return rows;
}

void TSortedDynamicStore::OnSetPassive()
{
    YT_VERIFY(FlushRevision_ == InvalidRevision);
    FlushRevision_ = GetLatestRevision();
}

void TSortedDynamicStore::OnSetRemoved()
{ }

TSortedDynamicRow TSortedDynamicStore::AllocateRow()
{
    return TSortedDynamicRow::Allocate(
        RowBuffer_->GetPool(),
        KeyColumnCount_,
        ColumnLockCount_,
        SchemaColumnCount_);
}

TSortedDynamicStore::TRowBlockedHandler TSortedDynamicStore::GetRowBlockedHandler()
{
    auto guard = ReaderGuard(RowBlockedLock_);
    return RowBlockedHandler_;
}

int TSortedDynamicStore::GetBlockingLockIndex(
    TSortedDynamicRow row,
    TLockMask lockMask,
    TTimestamp timestamp)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);

    lockMask.Enrich(ColumnLockCount_);

    const auto* lock = row.BeginLocks(KeyColumnCount_);
    for (int index = 0;
         index < ColumnLockCount_;
         ++index, ++lock)
    {
        auto lockType = lockMask.Get(index);

        if (lockType != ELockType::None && lock->PrepareTimestamp < timestamp) {
            return index;
        }
    }
    return -1;
}

bool TSortedDynamicStore::CheckRowBlocking(
    TSortedDynamicRow row,
    TLockMask lockMask,
    TWriteContext* context)
{
    auto timestamp = context->Transaction->GetStartTimestamp();
    int lockIndex = GetBlockingLockIndex(row, lockMask, timestamp);
    if (lockIndex < 0) {
        return true;
    }

    context->BlockedStore = this;
    context->BlockedRow = row;
    context->BlockedLockMask = lockMask;
    context->BlockedTimestamp = timestamp;
    return false;
}

TTimestamp TSortedDynamicStore::GetLastWriteTimestamp(TSortedDynamicRow row, int lockIndex)
{
    auto& lock = row.BeginLocks(KeyColumnCount_)[lockIndex];
    auto timestamp = GetLastTimestamp(TSortedDynamicRow::GetWriteRevisionList(lock));

    if (lockIndex == PrimaryLockIndex) {
        auto deleteTimestamp = GetLastTimestamp(row.GetDeleteRevisionList(KeyColumnCount_, ColumnLockCount_));
        timestamp = std::max(timestamp, deleteTimestamp);
    }

    return timestamp;
}

TTimestamp TSortedDynamicStore::GetLastReadTimestamp(TSortedDynamicRow row, int lockIndex)
{
    auto& lock = row.BeginLocks(KeyColumnCount_)[lockIndex];
    return lock.LastReadLockTimestamp;
}

TError TSortedDynamicStore::CheckRowLocks(
    TSortedDynamicRow row,
    TTransaction* transaction,
    TLockMask lockMask)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);

    // Check locks requested in #lockMask with the following exceptions:
    // * if primary lock is requested then all locks are checked
    // * primary lock is always checked

    // Enrich lock mask
    lockMask.Enrich(ColumnLockCount_);

    TError error;

    const auto* lock = row.BeginLocks(KeyColumnCount_);
    for (int index = 0; index < ColumnLockCount_; ++index, ++lock) {
        auto lockType = lockMask.Get(index);

        YT_VERIFY(lock->WriteTransaction != transaction);

        if (lockType == ELockType::Exclusive) {
            auto lastCommitTimestamp = GetLastReadTimestamp(row, index);
            if (lastCommitTimestamp > transaction->GetStartTimestamp()) {
                error = TError(
                    NTabletClient::EErrorCode::TransactionLockConflict,
                    "Write failed due to concurrent read lock")
                    << TErrorAttribute("winner_transaction_commit_timestamp", lastCommitTimestamp);
            }

            if (lock->ReadLockCount > 0) {
                YT_VERIFY(!lock->WriteTransaction);
                error = TError(
                     NTabletClient::EErrorCode::TransactionLockConflict,
                     "Write failed due to concurrent read lock");
            }
        }

        if (lockType != ELockType::None) {
            auto lastCommitTimestamp = GetLastWriteTimestamp(row, index);
            if (lastCommitTimestamp > transaction->GetStartTimestamp()) {
                error = TError(
                    NTabletClient::EErrorCode::TransactionLockConflict,
                    "Row lock conflict due to concurrent write")
                    << TErrorAttribute("winner_transaction_commit_timestamp", lastCommitTimestamp);
            }

            if (lock->WriteTransaction) {
                error = TError(
                    NTabletClient::EErrorCode::TransactionLockConflict,
                    "Row lock conflict due to concurrent write")
                    << TErrorAttribute("winner_transaction_id", lock->WriteTransaction->GetId());
            }
        }

        if (!error.IsOK()) {
            error = std::move(error)
                << TErrorAttribute("loser_transaction_id", transaction->GetId())
                << TErrorAttribute("tablet_id", TabletId_)
                << TErrorAttribute("table_path", TablePath_)
                << TErrorAttribute("key", RowToKey(row))
                << TErrorAttribute("lock", LockIndexToName_[index]);
            break;
        }
    }
    return error;
}

void TSortedDynamicStore::AcquireRowLocks(
    TSortedDynamicRow row,
    TLockMask lockMask,
    bool isDelete,
    TWriteContext* context)
{
    YT_ASSERT(Atomicity_ == EAtomicity::Full);

    // Acquire locks requested in #lockMask with the following exceptions:
    // * if primary lock is requested then all locks are acquired
    {
        auto* lock = row.BeginLocks(KeyColumnCount_);
        for (int index = 0; index < ColumnLockCount_; ++index, ++lock) {
            auto lockType = lockMask.Get(index);

            if (lockType != ELockType::None) {
                YT_VERIFY(!lock->WriteTransaction);
                YT_ASSERT(lock->PrepareTimestamp == NotPreparedTimestamp);
            }

            if (lockType == ELockType::Exclusive) {
                lock->WriteTransaction = context->Transaction;
                YT_ASSERT(lock->ReadLockCount == 0);
            } else if (lockType != ELockType::None) {
                ++lock->ReadLockCount;
            }
        }
    }

    if (isDelete) {
        YT_ASSERT(!row.GetDeleteLockFlag());
        row.SetDeleteLockFlag(true);
    }

    Lock();
}

void TSortedDynamicStore::AddDeleteRevision(TSortedDynamicRow row, ui32 revision)
{
    auto list = row.GetDeleteRevisionList(KeyColumnCount_, ColumnLockCount_);
    YT_ASSERT(!list || TimestampFromRevision(list.Back()) < TimestampFromRevision(revision));
    if (AllocateListForPushIfNeeded(&list, RowBuffer_->GetPool())) {
        row.SetDeleteRevisionList(list, KeyColumnCount_, ColumnLockCount_);
    }
    list.Push(revision);
}

void TSortedDynamicStore::AddWriteRevision(TLockDescriptor& lock, ui32 revision)
{
    auto list = TSortedDynamicRow::GetWriteRevisionList(lock);
    YT_ASSERT(!list || TimestampFromRevision(list.Back()) < TimestampFromRevision(revision));
    if (AllocateListForPushIfNeeded(&list, RowBuffer_->GetPool())) {
        TSortedDynamicRow::SetWriteRevisionList(lock, list);
    }
    list.Push(revision);
}

void TSortedDynamicStore::AddReadLockRevision(TLockDescriptor& lock, ui32 revision)
{
    auto list = TSortedDynamicRow::GetReadLockRevisionList(lock);
    auto timestamp = TimestampFromRevision(revision);
    lock.LastReadLockTimestamp = std::max(lock.LastReadLockTimestamp, timestamp);
    if (AllocateListForPushIfNeeded(&list, RowBuffer_->GetPool())) {
        TSortedDynamicRow::SetReadLockRevisionList(lock, list);
    }
    list.Push(revision);
}

void TSortedDynamicStore::SetKeys(TSortedDynamicRow dstRow, const TUnversionedValue* srcKeys)
{
    ui32 nullKeyMask = 0;
    ui32 nullKeyBit = 1;
    auto* dstValue = dstRow.BeginKeys();
    auto columnIt = Schema_->Columns().begin();
    for (int index = 0;
         index < KeyColumnCount_;
         ++index, nullKeyBit <<= 1, ++dstValue, ++columnIt)
    {
        const auto& srcValue = srcKeys[index];
        YT_ASSERT(srcValue.Id == index);
        dstRow.GetDataWeight() += GetDataWeight(srcValue);
        if (srcValue.Type == EValueType::Null) {
            nullKeyMask |= nullKeyBit;
        } else {
            YT_ASSERT(srcValue.Type == columnIt->GetWireType());
            if (IsStringLikeType(columnIt->GetWireType())) {
                *dstValue = CaptureStringValue(srcValue);
            } else {
                ::memcpy(dstValue, &srcValue.Data, sizeof(TDynamicValueData));
            }
        }
    }
    dstRow.SetNullKeyMask(nullKeyMask);
}

void TSortedDynamicStore::SetKeys(TSortedDynamicRow dstRow, TSortedDynamicRow srcRow)
{
    ui32 nullKeyMask = srcRow.GetNullKeyMask();
    dstRow.SetNullKeyMask(nullKeyMask);
    ui32 nullKeyBit = 1;
    const auto* srcKeys = srcRow.BeginKeys();
    auto* dstKeys = dstRow.BeginKeys();
    auto columnIt = Schema_->Columns().begin();
    for (int index = 0;
         index < KeyColumnCount_;
         ++index, nullKeyBit <<= 1, ++srcKeys, ++dstKeys, ++columnIt)
    {
        bool isNull = nullKeyMask & nullKeyBit;
        dstRow.GetDataWeight() += GetDataWeight(columnIt->GetWireType(), isNull, *srcKeys);
        if (!isNull) {
            if (IsStringLikeType(columnIt->GetWireType())) {
                *dstKeys = CaptureStringValue(*srcKeys);
            } else {
                *dstKeys = *srcKeys;
            }
        }
    }
}

void TSortedDynamicStore::AddValue(TSortedDynamicRow row, int index, TDynamicValue value)
{
    YT_ASSERT(index >= KeyColumnCount_ && index < SchemaColumnCount_);

    auto list = row.GetFixedValueList(index, KeyColumnCount_, ColumnLockCount_);
    if (AllocateListForPushIfNeeded(&list, RowBuffer_->GetPool())) {
        row.SetFixedValueList(index, list, KeyColumnCount_, ColumnLockCount_);
    }

    row.GetDataWeight() += GetDataWeight(Schema_->Columns()[index].GetWireType(), value);
    list.Push(std::move(value));

    ++StoreValueCount_;

    if (static_cast<ssize_t>(row.GetDataWeight()) > MaxDataWeight_) {
        MaxDataWeight_ = row.GetDataWeight();
        MaxDataWeightWitness_ = row;
    }
}

void TSortedDynamicStore::WriteRow(TSortedDynamicRow dynamicRow, TUnversionedRow row, ui32 revision)
{
    for (int index = KeyColumnCount_; index < static_cast<int>(row.GetCount()); ++index) {
        const auto& value = row[index];

        TDynamicValue dynamicValue;
        CaptureUnversionedValue(&dynamicValue, value);
        dynamicValue.Revision = revision;
        AddValue(dynamicRow, value.Id, std::move(dynamicValue));
    }
}

void TSortedDynamicStore::LoadRow(
    TVersionedRow row,
    TLoadScratchData* scratchData,
    TTimestamp* lastReadLockTimestamps)
{
    YT_ASSERT(row.GetKeyCount() == KeyColumnCount_);

    auto* timestampToRevision = &scratchData->TimestampToRevision;
    auto dynamicRow = AllocateRow();

    SetKeys(dynamicRow, row.BeginKeys());

    for (auto& revisions : scratchData->WriteRevisions) {
        revisions.clear();
    }

    const auto* currentValue = row.BeginValues();
    while (currentValue != row.EndValues()) {
        const auto* beginValue = currentValue;
        const auto* endValue = beginValue;
        int index = beginValue->Id;
        while (endValue != row.EndValues() && endValue->Id == index) {
            ++endValue;
        }

        int lockIndex = ColumnIndexToLockIndex_[index];
        // Values are ordered by descending timestamps but we need ascending ones here.
        for (const auto* value = endValue - 1; value >= beginValue; --value) {
            auto valueCopy = *value;
            DecodeInlineHunkInUnversionedValue(&valueCopy);
            TDynamicValue dynamicValue;
            ui32 revision = CaptureVersionedValue(&dynamicValue, valueCopy, timestampToRevision);
            AddValue(dynamicRow, index, std::move(dynamicValue));
            scratchData->WriteRevisions[lockIndex].push_back(revision);
        }

        currentValue = endValue;
    }

    auto* locks = dynamicRow.BeginLocks(KeyColumnCount_);
    for (int lockIndex = 0; lockIndex < ColumnLockCount_; ++lockIndex) {
        auto& lock = locks[lockIndex];
        auto& revisions = scratchData->WriteRevisions[lockIndex];
        if (!revisions.empty()) {
            std::sort(
                revisions.begin(),
                revisions.end(),
                [&] (ui32 lhs, ui32 rhs) {
                    return TimestampFromRevision(lhs) < TimestampFromRevision(rhs);
                });
            revisions.erase(
                std::unique(revisions.begin(), revisions.end()),
                revisions.end());
            for (ui32 revision : revisions) {
                AddWriteRevision(lock, revision);
            }
        }

        auto lastReadLockTimestamp = lastReadLockTimestamps[lockIndex];
        auto revision = CaptureTimestamp(lastReadLockTimestamp, timestampToRevision);
        AddReadLockRevision(lock, revision);
    }

    // Delete timestamps are also in descending order.
    if (row.GetDeleteTimestampCount() > 0) {
        for (const auto* currentTimestamp = row.EndDeleteTimestamps() - 1;
             currentTimestamp >= row.BeginDeleteTimestamps();
             --currentTimestamp)
        {
            ui32 revision = CaptureTimestamp(*currentTimestamp, timestampToRevision);
            AddDeleteRevision(dynamicRow, revision);
        }
    }

    Rows_->Insert(dynamicRow);

    InsertIntoLookupHashTable(row.BeginKeys(), dynamicRow);
}

ui32 TSortedDynamicStore::CaptureTimestamp(
    TTimestamp timestamp,
    TTimestampToRevisionMap* timestampToRevision)
{
    auto it = timestampToRevision->find(timestamp);
    if (it == timestampToRevision->end()) {
        ui32 revision = RegisterRevision(timestamp);
        YT_VERIFY(timestampToRevision->emplace(timestamp, revision).second);
        return revision;
    } else {
        return it->second;
    }
}

ui32 TSortedDynamicStore::CaptureVersionedValue(
    TDynamicValue* dst,
    const TVersionedValue& src,
    TTimestampToRevisionMap* timestampToRevision)
{
    YT_ASSERT(src.Type == EValueType::Null || src.Type == Schema_->Columns()[src.Id].GetWireType());
    ui32 revision = CaptureTimestamp(src.Timestamp, timestampToRevision);
    dst->Revision = revision;
    CaptureUnversionedValue(dst, src);
    return revision;
}

void TSortedDynamicStore::CaptureUnversionedValue(
    TDynamicValue* dst,
    const TUnversionedValue& src)
{
    YT_ASSERT(src.Type == EValueType::Null || src.Type == Schema_->Columns()[src.Id].GetWireType());

    dst->Flags = src.Flags;

    if (src.Type == EValueType::Null) {
        dst->Null = true;
        return;
    }

    dst->Null = false;

    if (IsStringLikeType(src.Type)) {
        dst->Data = CaptureStringValue(src);
    } else {
        ::memcpy(&dst->Data, &src.Data, sizeof(TDynamicValueData));
    }
}

TDynamicValueData TSortedDynamicStore::CaptureStringValue(TDynamicValueData src)
{
    ui32 length = src.String->Length;
    TDynamicValueData dst;
    dst.String = reinterpret_cast<TDynamicString*>(RowBuffer_->GetPool()->AllocateAligned(
        sizeof(ui32) + length,
        sizeof(ui32)));
    ::memcpy(dst.String, src.String, sizeof(ui32) + length);
    return dst;
}

TDynamicValueData TSortedDynamicStore::CaptureStringValue(const TUnversionedValue& src)
{
    YT_ASSERT(IsStringLikeType(src.Type));
    ui32 length = src.Length;
    TDynamicValueData dst;
    dst.String = reinterpret_cast<TDynamicString*>(RowBuffer_->GetPool()->AllocateAligned(
        sizeof(ui32) + length,
        sizeof(ui32)));
    dst.String->Length = length;
    ::memcpy(dst.String->Data, src.Data.String, length);
    return dst;
}

EStoreType TSortedDynamicStore::GetType() const
{
    return EStoreType::SortedDynamic;
}

i64 TSortedDynamicStore::GetRowCount() const
{
    return Rows_->GetSize();
}

i64 TSortedDynamicStore::GetTimestampCount() const
{
    return RevisionToTimestamp_.Size();
}

TLegacyOwningKey TSortedDynamicStore::GetMinKey() const
{
    return MinKey();
}

TLegacyOwningKey TSortedDynamicStore::GetUpperBoundKey() const
{
    return MaxKey();
}

bool TSortedDynamicStore::HasNontrivialReadRange() const
{
    return false;
}

IVersionedReaderPtr TSortedDynamicStore::CreateReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    TSharedRange<TRowRange> ranges,
    TTimestamp timestamp,
    bool produceAllVersions,
    const TColumnFilter& columnFilter,
    const TClientChunkReadOptions& /*chunkReadOptions*/,
    std::optional<EWorkloadCategory> /*workloadCategory*/)
{
    return CreateVersionedPerformanceCountingReader(
        New<TRangeReader>(
            this,
            tabletSnapshot,
            std::move(ranges),
            timestamp,
            produceAllVersions,
            /*snapshotMode*/ false,
            MaxRevision,
            columnFilter),
        tabletSnapshot->PerformanceCounters,
        NTableClient::EDataSource::DynamicStore,
        ERequestType::Read);
}

IVersionedReaderPtr TSortedDynamicStore::CreateReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    TSharedRange<TLegacyKey> keys,
    TTimestamp timestamp,
    bool produceAllVersions,
    const TColumnFilter& columnFilter,
    const TClientChunkReadOptions& /*chunkReadOptions*/,
    std::optional<EWorkloadCategory> /*workloadCategory*/)
{
    return CreateVersionedPerformanceCountingReader(
        New<TLookupReader>(
            this,
            tabletSnapshot,
            std::move(keys),
            timestamp,
            produceAllVersions,
            columnFilter),
        tabletSnapshot->PerformanceCounters,
        NTableClient::EDataSource::DynamicStore,
        ERequestType::Lookup);
}

bool TSortedDynamicStore::CheckRowLocks(
    TUnversionedRow row,
    TLockMask lockMask,
    TWriteContext* context)
{
    auto it = Rows_->FindEqualTo(ToKeyRef(row, KeyColumnCount_));
    if (!it.IsValid()) {
        return true;
    }

    auto dynamicRow = it.GetCurrent();

    if (context->Phase == EWritePhase::Prelock && !CheckRowBlocking(dynamicRow, lockMask, context)) {
        return false;
    }

    auto error = CheckRowLocks(dynamicRow, context->Transaction, lockMask);
    if (!error.IsOK()) {
        context->Error = error;
        return false;
    }

    return true;
}

void TSortedDynamicStore::Save(TSaveContext& context) const
{
    TStoreBase::Save(context);

    using NYT::Save;
    Save(context, MinTimestamp_);
    Save(context, MaxTimestamp_);
    Save(context, MergeRowsOnFlushAllowed_);
}

void TSortedDynamicStore::Load(TLoadContext& context)
{
    TStoreBase::Load(context);

    using NYT::Load;
    Load(context, MinTimestamp_);
    Load(context, MaxTimestamp_);
    Load(context, MergeRowsOnFlushAllowed_);
}

TCallback<void(TSaveContext& context)> TSortedDynamicStore::AsyncSave()
{
    using NYT::Save;

    auto tableReader = CreateSnapshotReader();
    auto revision = GetLatestRevision();

    return BIND([=, this, this_ = MakeStrong(this)] (TSaveContext& context) {
        YT_LOG_DEBUG("Store snapshot serialization started");

        YT_LOG_DEBUG("Opening table reader");
        WaitFor(tableReader->Open())
            .ThrowOnError();

        auto chunkWriter = New<TMemoryWriter>();

        auto tableWriterConfig = New<TChunkWriterConfig>();
        tableWriterConfig->WorkloadDescriptor = TWorkloadDescriptor(EWorkloadCategory::SystemTabletRecovery);
        // Ensure deterministic snapshots.
        tableWriterConfig->SampleRate = 0.0;
        tableWriterConfig->Postprocess();

        auto tableWriterOptions = New<TTabletStoreWriterOptions>();
        tableWriterOptions->OptimizeFor = EOptimizeFor::Scan;
        // Ensure deterministic snapshots.
        tableWriterOptions->SetChunkCreationTime = false;
        tableWriterOptions->Postprocess();

        auto tableWriter = CreateVersionedChunkWriter(
            tableWriterConfig,
            tableWriterOptions,
            Schema_,
            chunkWriter,
            /*dataSink*/ std::nullopt);

        TRowBatchReadOptions options{
            .MaxRowsPerRead = SnapshotRowsPerRead
        };

        YT_LOG_DEBUG("Serializing store snapshot");

        std::vector<TTimestamp> lastReadLockTimestamps;

        auto rowIt = Rows_->FindGreaterThanOrEqualTo(ToKeyRef(MinKey()));
        i64 rowCount = 0;
        while (auto batch = tableReader->Read(options)) {
            if (batch->IsEmpty()) {
                YT_LOG_DEBUG("Waiting for table reader");
                WaitFor(tableReader->GetReadyEvent())
                    .ThrowOnError();
                continue;
            }

            rowCount += batch->GetRowCount();
            auto rows = batch->MaterializeRows();
            for (auto row : rows) {
                auto key = row.Keys();
                auto dynamicRow = rowIt.GetCurrent();
                while (RowKeyComparer_(dynamicRow, key) < 0) {
                    rowIt.MoveNext();
                    YT_VERIFY(rowIt.IsValid());
                    dynamicRow = rowIt.GetCurrent();
                }
                YT_VERIFY(RowKeyComparer_(dynamicRow, key) == 0);
                for (int index = 0; index < ColumnLockCount_; ++index) {
                    auto& lock = dynamicRow.BeginLocks(KeyColumnCount_)[index];
                    auto readLockRevisionList = TSortedDynamicRow::GetReadLockRevisionList(lock);
                    auto lastReadLockTimestamp = GetLastTimestamp(readLockRevisionList, revision);
                    lastReadLockTimestamps.push_back(lastReadLockTimestamp);
                }
            }

            if (!tableWriter->Write(std::move(rows))) {
                YT_LOG_DEBUG("Waiting for table writer");
                WaitFor(tableWriter->GetReadyEvent())
                    .ThrowOnError();
            }
        }

        // psushin@ forbids empty chunks.
        if (rowCount == 0) {
            Save(context, false);
            return;
        }

        Save(context, true);

        YT_VERIFY(std::ssize(lastReadLockTimestamps) == rowCount * ColumnLockCount_);
        Save(context, lastReadLockTimestamps);

        // NB: This also closes chunkWriter.
        YT_LOG_DEBUG("Closing table writer");
        WaitFor(tableWriter->Close())
            .ThrowOnError();

        Save(context, *chunkWriter->GetChunkMeta());

        auto blocks = TBlock::Unwrap(chunkWriter->GetBlocks());
        YT_LOG_DEBUG("Writing store blocks (RowCount: %v, BlockCount: %v)",
            rowCount,
            blocks.size());

        Save(context, blocks);

        YT_LOG_DEBUG("Store snapshot serialization complete");
    });
}

void TSortedDynamicStore::AsyncLoad(TLoadContext& context)
{
    using NYT::Load;

    if (Load<bool>(context)) {
        auto lastReadLockTimestamps = Load<std::vector<TTimestamp>>(context);
        YT_VERIFY(!lastReadLockTimestamps.empty());
        auto lastReadLockTimestampPtr = lastReadLockTimestamps.begin();

        auto chunkMeta = New<TRefCountedChunkMeta>(Load<TChunkMeta>(context));
        auto blocks = Load<std::vector<TSharedRef>>(context);

        auto chunkReader = CreateMemoryReader(
            std::move(chunkMeta),
            TBlock::Wrap(blocks));

        auto metaMemoryTracker = MemoryTracker_
            ? MemoryTracker_->WithCategory(EMemoryCategory::VersionedChunkMeta)
            : nullptr;

        auto cachedMetaFuture = chunkReader->GetMeta(/*chunkReadOptions*/ {})
            .Apply(BIND(
                &TCachedVersionedChunkMeta::Create,
                /*prepareColumnarMeta*/ false,
                metaMemoryTracker));
        auto cachedMeta = WaitFor(cachedMetaFuture)
            .ValueOrThrow();

        TChunkSpec chunkSpec;
        ToProto(chunkSpec.mutable_chunk_id(), StoreId_);

        auto chunkState = New<TChunkState>(TChunkState{
            .BlockCache = GetNullBlockCache(),
            .TableSchema = Schema_,
        });

        auto tableReaderConfig = New<TTabletStoreReaderConfig>();

        auto tableReader = CreateVersionedChunkReader(
            tableReaderConfig,
            chunkReader,
            std::move(chunkState),
            std::move(cachedMeta),
            /*chunkReadOptions*/ {},
            MinKey(),
            MaxKey(),
            TColumnFilter(),
            AllCommittedTimestamp,
            true);
        WaitFor(tableReader->Open())
            .ThrowOnError();

        TRowBatchReadOptions options{
            .MaxRowsPerRead = SnapshotRowsPerRead
        };

        TLoadScratchData scratchData;
        scratchData.WriteRevisions.resize(ColumnLockCount_);

        while (auto batch = tableReader->Read(options)) {
            if (batch->IsEmpty()) {
                WaitFor(tableReader->GetReadyEvent())
                    .ThrowOnError();
                continue;
            }

            for (auto row : batch->MaterializeRows()) {
                LoadRow(row, &scratchData, lastReadLockTimestampPtr);
                lastReadLockTimestampPtr += ColumnLockCount_;
            }
        }
    }

    // Cf. YT-4534
    if (StoreState_ == EStoreState::PassiveDynamic ||
        StoreState_ == EStoreState::RemovePrepared)
    {
        // NB: No more changes are possible after load.
        YT_VERIFY(FlushRevision_ == InvalidRevision);
        FlushRevision_ = MaxRevision;
    }

    OnDynamicMemoryUsageUpdated();
}

TSortedDynamicStorePtr TSortedDynamicStore::AsSortedDynamic()
{
    return this;
}

void TSortedDynamicStore::SetBackupCheckpointTimestamp(TTimestamp /*timestamp*/)
{
    MergeRowsOnFlushAllowed_ = false;
}

bool TSortedDynamicStore::IsMergeRowsOnFlushAllowed() const
{
    return MergeRowsOnFlushAllowed_;
}

TTimestamp TSortedDynamicStore::GetLastTimestamp(TRevisionList list) const
{
    if (list) {
        int size = list.GetSize();
        if (size > 0) {
            return TimestampFromRevision(list[size - 1]);
        }
    }

    return MinTimestamp;
}

TTimestamp TSortedDynamicStore::GetLastTimestamp(TRevisionList list, ui32 revision) const
{
    auto lastTimestamp = MinTimestamp;
    while (list) {
        const auto* begin = list.Begin();
        const auto* end = list.End();
        for (const auto* current = begin; current != end; ++current) {
            if (*current <= revision) {
                lastTimestamp = std::max(lastTimestamp, TimestampFromRevision(*current));
            }
        }
        list = list.GetSuccessor();
    }

    return lastTimestamp;
}

ui32 TSortedDynamicStore::GetLatestRevision() const
{
    YT_ASSERT(!RevisionToTimestamp_.Empty());
    return RevisionToTimestamp_.Size() - 1;
}

ui32 TSortedDynamicStore::RegisterRevision(TTimestamp timestamp)
{
    YT_VERIFY(timestamp >= MinTimestamp && timestamp <= MaxTimestamp);

    i64 mutationSequenceNumber = 0;
    if (auto* mutationContext = TryGetCurrentMutationContext()) {
        mutationSequenceNumber = mutationContext->GetSequenceNumber();
    }

    auto latestRevision = GetLatestRevision();
    if (mutationSequenceNumber == LatestRevisionMutationSequenceNumber_ &&
        TimestampFromRevision(latestRevision) == timestamp)
    {
        return latestRevision;
    }

    YT_VERIFY(RevisionToTimestamp_.Size() < HardRevisionsPerDynamicStoreLimit);
    RevisionToTimestamp_.PushBack(timestamp);
    LatestRevisionMutationSequenceNumber_ = mutationSequenceNumber;

    return GetLatestRevision();
}

void TSortedDynamicStore::OnDynamicMemoryUsageUpdated()
{
    auto hashTableSize = LookupHashTable_ ? LookupHashTable_->GetByteSize() : 0;
    SetDynamicMemoryUsage(GetUncompressedDataSize() + hashTableSize);
}

void TSortedDynamicStore::InsertIntoLookupHashTable(
    const TUnversionedValue* keyBegin,
    TSortedDynamicRow dynamicRow)
{
    if (LookupHashTable_) {
        if (GetRowCount() >= LookupHashTable_->GetSize()) {
            LookupHashTable_.reset();
        } else {
            LookupHashTable_->Insert(keyBegin, dynamicRow);
        }
    }
}

i64 TSortedDynamicStore::GetMaxDataWeight() const
{
    return MaxDataWeight_;
}

TLegacyOwningKey TSortedDynamicStore::GetMaxDataWeightWitnessKey() const
{
    return RowToKey(MaxDataWeightWitness_);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
