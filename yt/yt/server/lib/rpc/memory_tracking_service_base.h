#pragma once

#include <yt/yt/core/rpc/service_detail.h>
#include <yt/yt/core/misc/memory_usage_tracker.h>

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

namespace NYT::NRpc {

////////////////////////////////////////////////////////////////////////////////

template <class TBaseService>
class TMemoryTrackingServiceBase
    : public TBaseService
{
public:
    template <typename... TArgs>
    TMemoryTrackingServiceBase(
        IMemoryUsageTrackerPtr memoryTracker,
        TArgs&&... args);

protected:
    void HandleRequest(
        std::unique_ptr<NRpc::NProto::TRequestHeader> header,
        TSharedRefArray message,
        NBus::IBusPtr replyBus) override;

private:
    IMemoryUsageTrackerPtr MemoryTracker_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc

#define MEMORY_TRACKING_SERVICE_BASE_H_
#include "memory_tracking_service_base-inl.h"
#undef MEMORY_TRACKING_SERVICE_BASE_H_
