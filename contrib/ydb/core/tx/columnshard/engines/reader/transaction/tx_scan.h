#pragma once
#include <contrib/ydb/core/tablet_flat/tablet_flat_executor.h>
#include <contrib/ydb/core/tx/columnshard/columnshard_impl.h>
#include <contrib/ydb/core/tx/columnshard/engines/reader/abstract/read_metadata.h>

namespace NKikimr::NOlap::NReader {
class TTxScan: public NTabletFlatExecutor::TTransactionBase<NColumnShard::TColumnShard> {
private:
    using TBase = NTabletFlatExecutor::TTransactionBase<NColumnShard::TColumnShard>;
    void SendError(const TString& problem, const TString& details, const TActorContext& ctx) const;

public:
    using TReadMetadataPtr = TReadMetadataBase::TConstPtr;

    TTxScan(NColumnShard::TColumnShard* self, TEvDataShard::TEvKqpScan::TPtr& ev)
        : TBase(self)
        , Ev(ev) {
    }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override;
    void Complete(const TActorContext& ctx) override;
    TTxType GetTxType() const override {
        return NColumnShard::TXTYPE_START_SCAN;
    }

private:
    TEvDataShard::TEvKqpScan::TPtr Ev;
};

}   // namespace NKikimr::NOlap::NReader
