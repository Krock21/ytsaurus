#include "special_keys.h"
#include "permutations.h"
#include "size_calcer.h"

#include "reader/position.h"

#include <contrib/ydb/core/formats/arrow/serializer/abstract.h>
#include <contrib/ydb/core/formats/arrow/arrow_helpers.h>
#include <contrib/ydb/core/formats/arrow/arrow_filter.h>

namespace NKikimr::NArrow {

bool TSpecialKeys::DeserializeFromString(const TString& data) {
    if (!data) {
        return false;
    }
    Data = NArrow::TStatusValidator::GetValid(NArrow::NSerialization::TSerializerContainer::GetDefaultSerializer()->Deserialize(data));
    return !!Data;
}

NKikimr::NArrow::TReplaceKey TSpecialKeys::GetKeyByIndex(const ui32 position, const std::shared_ptr<arrow::Schema>& schema) const {
    Y_ABORT_UNLESS(position < Data->num_rows());
    if (schema) {
        return NArrow::TReplaceKey::FromBatch(Data, schema, position);
    } else {
        return NArrow::TReplaceKey::FromBatch(Data, position);
    }
}

TString TSpecialKeys::SerializePayloadToString() const {
    return NArrow::NSerialization::TSerializerContainer::GetFastestSerializer()->SerializePayload(Data);
}

TString TSpecialKeys::SerializeFullToString() const {
    return NArrow::NSerialization::TSerializerContainer::GetFastestSerializer()->SerializeFull(Data);
}

ui64 TSpecialKeys::GetMemoryBytes() const {
    return Data ? NArrow::GetBatchDataSize(Data) : 0;
}

ui64 TSpecialKeys::GetMemorySize() const {
    return GetBatchMemorySize(Data);
}

TFirstLastSpecialKeys::TFirstLastSpecialKeys(const std::shared_ptr<arrow::RecordBatch>& batch, const std::vector<TString>& columnNames /*= {}*/) {
    Y_ABORT_UNLESS(batch);
    Y_ABORT_UNLESS(batch->num_rows());
    std::shared_ptr<arrow::RecordBatch> keyBatch = batch;
    if (columnNames.size()) {
        keyBatch = NArrow::TColumnOperator().VerifyIfAbsent().Extract(batch, columnNames);
    }
    if (keyBatch->num_rows() <= 2) {
        Data = keyBatch;
    } else {
        std::vector<ui64> indexes = { 0 };
        if (batch->num_rows() > 1) {
            indexes.emplace_back(batch->num_rows() - 1);
        }

        Data = NArrow::CopyRecords(keyBatch, indexes);
        Y_ABORT_UNLESS(Data->num_rows() == 1 || Data->num_rows() == 2);
    }
}

TMinMaxSpecialKeys::TMinMaxSpecialKeys(std::shared_ptr<arrow::RecordBatch> batch, const std::shared_ptr<arrow::Schema>& schema) {
    Y_ABORT_UNLESS(batch);
    Y_ABORT_UNLESS(batch->num_rows());
    Y_ABORT_UNLESS(schema);

    NMerger::TRWSortableBatchPosition record(batch, 0, schema->field_names(), {}, false);
    std::optional<NMerger::TCursor> minValue;
    std::optional<NMerger::TCursor> maxValue;
    while (true) {
        if (!minValue || record.Compare(*minValue) == std::partial_ordering::less) {
            minValue = record.BuildSortingCursor();
        }
        if (!maxValue || record.Compare(*maxValue) == std::partial_ordering::greater) {
            maxValue = record.BuildSortingCursor();
        }
        if (!record.NextPosition(1)) {
            break;
        }
    }
    Y_ABORT_UNLESS(minValue && maxValue);
    std::vector<ui64> indexes;
    indexes.emplace_back(minValue->GetPosition());
    if (maxValue->GetPosition() != minValue->GetPosition()) {
        indexes.emplace_back(maxValue->GetPosition());
    }

    std::vector<TString> columnNamesString;
    for (auto&& i : schema->field_names()) {
        columnNamesString.emplace_back(i);
    }

    auto dataBatch = NArrow::TColumnOperator().VerifyIfAbsent().Extract(batch, columnNamesString);
    Data = NArrow::CopyRecords(dataBatch, indexes);
    Y_ABORT_UNLESS(Data->num_rows() == 1 || Data->num_rows() == 2);
}

TFirstLastSpecialKeys::TFirstLastSpecialKeys(const TString& data)
    : TBase(data) {
    Y_DEBUG_ABORT_UNLESS(Data->ValidateFull().ok());
    Y_ABORT_UNLESS(Data->num_rows() == 1 || Data->num_rows() == 2);
}

std::shared_ptr<NKikimr::NArrow::TFirstLastSpecialKeys> TFirstLastSpecialKeys::BuildAccordingToSchemaVerified(const std::shared_ptr<arrow::Schema>& schema) const {
    auto newData = NArrow::TColumnOperator().Adapt(Data, schema).DetachResult();
    return std::make_shared<TFirstLastSpecialKeys>(newData);
}

TMinMaxSpecialKeys::TMinMaxSpecialKeys(const TString& data)
    : TBase(data)
{
    Y_DEBUG_ABORT_UNLESS(Data->ValidateFull().ok());
    Y_ABORT_UNLESS(Data->num_rows() == 1 || Data->num_rows() == 2);
}

std::shared_ptr<NKikimr::NArrow::TMinMaxSpecialKeys> TMinMaxSpecialKeys::BuildAccordingToSchemaVerified(const std::shared_ptr<arrow::Schema>& schema) const {
    auto newData = NArrow::TColumnOperator().Adapt(Data, schema).DetachResult();
    return std::shared_ptr<TMinMaxSpecialKeys>(new TMinMaxSpecialKeys(newData));
}

}
