#include "gpu_info_provider.h"

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NJobAgent {

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

void FormatValue(TStringBuilderBase* builder, const TCondition& condition, TStringBuf /*format*/)
{
    builder->AppendFormat("{Status: %v, LastTransitionTime: %v}",
        condition.Status,
        condition.LastTransitionTime);
}

void Serialize(const TCondition& condition, NYson::IYsonConsumer* consumer)
{
    NYTree::BuildYsonFluently(consumer)
        .BeginMap()
            .Item("status").Value(condition.Status)
            .OptionalItem("last_transition_time", condition.LastTransitionTime)
        .EndMap();
}

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TGpuInfo& gpuInfo, TStringBuf /*format*/)
{
    builder->AppendFormat(
        "{UpdateTime: %v, Index: %v, Name: %v, UtilizationGpuRate: %v, UtilizationMemoryRate: %v, "
        "MemoryUsed: %v, MemoryTotal: %v, PowerDraw: %v, PowerLimit: %v, ClocksSM: %v, ClocksMaxSM: %v, "
        "SMUtilizationRate: %v, SMOccupancyRate: %v, Stuck: %v}",
        gpuInfo.UpdateTime,
        gpuInfo.Index,
        gpuInfo.Name,
        gpuInfo.UtilizationGpuRate,
        gpuInfo.UtilizationMemoryRate,
        gpuInfo.MemoryUsed,
        gpuInfo.MemoryTotal,
        gpuInfo.PowerDraw,
        gpuInfo.PowerLimit,
        gpuInfo.ClocksSM,
        gpuInfo.ClocksMaxSM,
        gpuInfo.SMUtilizationRate,
        gpuInfo.SMOccupancyRate,
        gpuInfo.Stuck);
}

void Serialize(const TGpuInfo& gpuInfo, NYson::IYsonConsumer* consumer)
{
    NYTree::BuildYsonFluently(consumer)
        .BeginMap()
            .Item("update_time").Value(gpuInfo.UpdateTime)
            .Item("index").Value(gpuInfo.Index)
            .Item("name").Value(gpuInfo.Name)
            .Item("utilization_gpu_rate").Value(gpuInfo.UtilizationGpuRate)
            .Item("utilization_memory_rate").Value(gpuInfo.UtilizationMemoryRate)
            .Item("memory_used").Value(gpuInfo.MemoryUsed)
            .Item("memory_limit").Value(gpuInfo.MemoryTotal)
            .Item("power_draw").Value(gpuInfo.PowerDraw)
            .Item("power_limit").Value(gpuInfo.PowerLimit)
            .Item("clocks_sm").Value(gpuInfo.ClocksSM)
            .Item("clocks_max_sm").Value(gpuInfo.ClocksMaxSM)
            .Item("sm_utilization_rate").Value(gpuInfo.SMUtilizationRate)
            .Item("sm_occupancy_rate").Value(gpuInfo.SMOccupancyRate)
            .Item("stuck").Value(gpuInfo.Stuck)
        .EndMap();
}

////////////////////////////////////////////////////////////////////////////////

struct TGpuInfoProviderMock
    : public IGpuInfoProvider
{
    virtual std::vector<TGpuInfo> GetGpuInfos(TDuration /*checkTimeout*/)
    {
        THROW_ERROR_EXCEPTION("GPU info provider library is not available under this build configuration");
    }
};

DEFINE_REFCOUNTED_TYPE(TGpuInfoProviderMock)

////////////////////////////////////////////////////////////////////////////////

Y_WEAK IGpuInfoProviderPtr CreateGpuInfoProvider(const TGpuInfoSourceConfigPtr& /*config*/)
{
    return New<TGpuInfoProviderMock>();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NY::NJobAgent
