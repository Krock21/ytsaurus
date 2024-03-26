#pragma once

#include "public.h"

#include <yt/yt/server/lib/hydra_common/public.h>
#include <yt/yt/server/lib/hydra_common/private.h>

#include <yt/yt/ytlib/hydra/private.h>

#include <yt/yt/library/profiling/sensor.h>

#include <library/cpp/yt/memory/atomic_intrusive_ptr.h>

namespace NYT::NHydra2 {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TDecoratedAutomaton)
DECLARE_REFCOUNTED_CLASS(TRecovery)
DECLARE_REFCOUNTED_CLASS(TLeaderLease)
DECLARE_REFCOUNTED_CLASS(TLeaseTracker)
DECLARE_REFCOUNTED_CLASS(TLeaderCommitter)
DECLARE_REFCOUNTED_CLASS(TFollowerCommitter)
DECLARE_REFCOUNTED_CLASS(TConfigWrapper)

DECLARE_REFCOUNTED_STRUCT(TEpochContext)
DECLARE_REFCOUNTED_STRUCT(IChangelogDiscarder)
DECLARE_REFCOUNTED_STRUCT(TPendingMutation)

////////////////////////////////////////////////////////////////////////////////

class TConfigWrapper
    : public TRefCounted
{
public:
    explicit TConfigWrapper(NHydra::TDistributedHydraManagerConfigPtr config);

    void Set(NHydra::TDistributedHydraManagerConfigPtr config);
    NHydra::TDistributedHydraManagerConfigPtr Get() const;

private:
    TAtomicIntrusivePtr<NHydra::TDistributedHydraManagerConfig> Config_;
};

DEFINE_REFCOUNTED_TYPE(TConfigWrapper)

////////////////////////////////////////////////////////////////////////////////

bool IsSystemMutationType(const TString& mutationType);

////////////////////////////////////////////////////////////////////////////////

using NElection::TCellId;
using NElection::NullCellId;
using NElection::TPeerId;
using NElection::InvalidPeerId;
using NElection::TPeerPriority;
using NElection::TEpochId;

using NHydra::EPeerState;
using NHydra::EErrorCode;
using NHydra::EFinalRecoveryAction;

using NHydra::HydraLogger;

using NHydra::TVersion;
using NHydra::TElectionPriority;
using NHydra::TReachableState;

using NHydra::InvalidPeerId;
using NHydra::TReign;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra2
