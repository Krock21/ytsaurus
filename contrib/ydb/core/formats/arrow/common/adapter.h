#pragma once
#include "container.h"

#include <contrib/ydb/core/formats/arrow/accessor/plain/accessor.h>
#include <contrib/ydb/core/formats/arrow/arrow_filter.h>

#include <contrib/ydb/library/formats/arrow/arrow_helpers.h>
#include <contrib/ydb/library/formats/arrow/validation/validation.h>
#include <contrib/ydb/library/yverify_stream/yverify_stream.h>

#include <contrib/libs/apache/arrow/cpp/src/arrow/array/array_base.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/array/array_primitive.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/chunked_array.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/compute/api_vector.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/datum.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/record_batch.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/table.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/type.h>

namespace NKikimr::NArrow::NAdapter {

template <class T>
class TDataBuilderPolicy {
public:
};

template <>
class TDataBuilderPolicy<arrow::RecordBatch> {
public:
    using TColumn = arrow::Array;
    using TAccessor = NAccessor::TTrivialArray;

    [[nodiscard]] static std::shared_ptr<arrow::RecordBatch> AddColumn(const std::shared_ptr<arrow::RecordBatch>& batch,
        const std::shared_ptr<arrow::Field>& field, const std::shared_ptr<arrow::Array>& extCol) {
        return TStatusValidator::GetValid(batch->AddColumn(batch->num_columns(), field, extCol));
    }

    [[nodiscard]] static std::shared_ptr<arrow::RecordBatch> Build(std::vector<std::shared_ptr<arrow::Field>>&& fields, std::vector<std::shared_ptr<TColumn>>&& columns, const ui32 count) {
        return arrow::RecordBatch::Make(std::make_shared<arrow::Schema>(std::move(fields)), count, std::move(columns));
    }
    [[nodiscard]] static std::shared_ptr<arrow::RecordBatch> Build(const std::shared_ptr<arrow::Schema>& schema, std::vector<std::shared_ptr<TColumn>>&& columns, const ui32 count) {
        return arrow::RecordBatch::Make(schema, count, std::move(columns));
    }
    [[nodiscard]] static std::shared_ptr<arrow::RecordBatch> ApplyArrowFilter(
        const std::shared_ptr<arrow::RecordBatch>& batch, const TColumnFilter& filter) {
        auto res = arrow::compute::Filter(batch, filter.BuildArrowFilter(batch->num_rows()));
        Y_VERIFY_S(res.ok(), res.status().message());
        Y_ABORT_UNLESS(res->kind() == arrow::Datum::RECORD_BATCH);
        return res->record_batch();
    }
    [[nodiscard]] static std::shared_ptr<arrow::RecordBatch> ApplySlicesFilter(
        const std::shared_ptr<arrow::RecordBatch>& batch, TColumnFilter::TSlicesIterator filter) {
        AFL_VERIFY(filter.GetRecordsCount() == batch->num_rows());
        std::vector<std::shared_ptr<arrow::RecordBatch>> slices;
        for (filter.Start(); filter.IsValid(); filter.Next()) {
            if (!filter.IsFiltered()) {
                continue;
            }
            slices.emplace_back(batch->Slice(filter.GetStartIndex(), filter.GetSliceSize()));
        }
        return NArrow::ToBatch(TStatusValidator::GetValid(arrow::Table::FromRecordBatches(slices)));
    }
    [[nodiscard]] static std::shared_ptr<arrow::RecordBatch> GetEmptySame(const std::shared_ptr<arrow::RecordBatch>& batch) {
        return batch->Slice(0, 0);
    }
};

template <>
class TDataBuilderPolicy<arrow::Table> {
public:
    using TColumn = arrow::ChunkedArray;
    using TAccessor = NAccessor::TTrivialChunkedArray;
    [[nodiscard]] static std::shared_ptr<arrow::Table> Build(std::vector<std::shared_ptr<arrow::Field>>&& fields, std::vector<std::shared_ptr<TColumn>>&& columns, const ui32 count) {
        return arrow::Table::Make(std::make_shared<arrow::Schema>(std::move(fields)), std::move(columns), count);
    }
    [[nodiscard]] static std::shared_ptr<arrow::Table> Build(const std::shared_ptr<arrow::Schema>& schema, std::vector<std::shared_ptr<TColumn>>&& columns, const ui32 count) {
        return arrow::Table::Make(schema, std::move(columns), count);
    }
    [[nodiscard]] static std::shared_ptr<arrow::Table> AddColumn(
        const std::shared_ptr<arrow::Table>& batch, const std::shared_ptr<arrow::Field>& field, const std::shared_ptr<arrow::Array>& extCol) {
        return TStatusValidator::GetValid(batch->AddColumn(batch->num_columns(), field, std::make_shared<arrow::ChunkedArray>(extCol)));
    }

    [[nodiscard]] static std::shared_ptr<arrow::Table> ApplyArrowFilter(
        const std::shared_ptr<arrow::Table>& batch, const TColumnFilter& filter) {
        auto res = arrow::compute::Filter(batch, filter.BuildArrowFilter(batch->num_rows()));
        Y_VERIFY_S(res.ok(), res.status().message());
        Y_ABORT_UNLESS(res->kind() == arrow::Datum::TABLE);
        return res->table();
    }
    [[nodiscard]] static std::shared_ptr<arrow::Table> ApplySlicesFilter(
        const std::shared_ptr<arrow::Table>& batch, TColumnFilter::TSlicesIterator filter) {
        std::vector<std::shared_ptr<arrow::Table>> slices;
        for (filter.Start(); filter.IsValid(); filter.Next()) {
            if (!filter.IsFiltered()) {
                continue;
            }
            slices.emplace_back(batch->Slice(filter.GetStartIndex(), filter.GetSliceSize()));
        }
        return TStatusValidator::GetValid(arrow::ConcatenateTables(slices));
    }
    [[nodiscard]] static std::shared_ptr<arrow::Table> GetEmptySame(const std::shared_ptr<arrow::Table>& batch) {
        return batch->Slice(0, 0);
    }
};

template <>
class TDataBuilderPolicy<TGeneralContainer> {
public:
    using TColumn = NAccessor::IChunkedArray;
    [[nodiscard]] static std::shared_ptr<TGeneralContainer> Build(std::vector<std::shared_ptr<arrow::Field>>&& fields, std::vector<std::shared_ptr<TColumn>>&& columns, const ui32 count) {
        Y_ABORT_UNLESS(columns.size());
        for (auto&& i : columns) {
            Y_ABORT_UNLESS(i->GetRecordsCount() == count);
        }
        return std::make_shared<TGeneralContainer>(std::make_shared<arrow::Schema>(std::move(fields)), std::move(columns));
    }
    [[nodiscard]] static std::shared_ptr<TGeneralContainer> AddColumn(const std::shared_ptr<TGeneralContainer>& batch,
        const std::shared_ptr<arrow::Field>& field, const std::shared_ptr<arrow::Array>& extCol) {
        batch->AddField(field, std::make_shared<NAccessor::TTrivialArray>(extCol)).Validate();
        return batch;
    }
    [[nodiscard]] static std::shared_ptr<TGeneralContainer> ApplyArrowFilter(
        const std::shared_ptr<TGeneralContainer>& batch, const TColumnFilter& filter) {
        return std::make_shared<TGeneralContainer>(batch->ApplyFilter(filter));
    }
    [[nodiscard]] static std::shared_ptr<TGeneralContainer> ApplySlicesFilter(
        const std::shared_ptr<TGeneralContainer>& batch, TColumnFilter::TSlicesIterator filter) {
        auto table = batch->BuildTableVerified();
        return std::make_shared<TGeneralContainer>(TDataBuilderPolicy<arrow::Table>::ApplySlicesFilter(table, filter));
    }
    [[nodiscard]] static std::shared_ptr<TGeneralContainer> GetEmptySame(const std::shared_ptr<TGeneralContainer>& batch) {
        return batch->BuildEmptySame();
    }
};

}   // namespace NKikimr::NArrow::NAdapter
