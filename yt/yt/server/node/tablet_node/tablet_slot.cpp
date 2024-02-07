#include "tablet_slot.h"

#include "automaton.h"
#include "bootstrap.h"
#include "distributed_throttler_manager.h"
#include "hint_manager.h"
#include "hunk_tablet_manager.h"
#include "master_connector.h"
#include "mutation_forwarder.h"
#include "mutation_forwarder_thunk.h"
#include "private.h"
#include "security_manager.h"
#include "serialize.h"
#include "slot_manager.h"
#include "smooth_movement_tracker.h"
#include "tablet.h"
#include "tablet_cell_write_manager.h"
#include "tablet_manager.h"
#include "tablet_service.h"
#include "tablet_snapshot_store.h"
#include "transaction_manager.h"
#include "overload_controller.h"

#include <yt/yt/server/node/data_node/config.h>

#include <yt/yt/server/lib/cellar_agent/automaton_invoker_hood.h>
#include <yt/yt/server/lib/cellar_agent/occupant.h>

#include <yt/yt/server/lib/election/election_manager.h>

#include <yt/yt/server/lib/hive/helpers.h>
#include <yt/yt/server/lib/hive/hive_manager.h>
#include <yt/yt/server/lib/hive/mailbox.h>
#include <yt/yt/server/lib/hive/avenue_directory.h>

#include <yt/yt/server/lib/hydra/remote_changelog_store.h>
#include <yt/yt/server/lib/hydra/remote_snapshot_store.h>
#include <yt/yt/server/lib/hydra/distributed_hydra_manager.h>

#include <yt/yt/server/lib/transaction_supervisor/transaction_supervisor.h>
#include <yt/yt/server/lib/transaction_supervisor/transaction_lease_tracker.h>
#include <yt/yt/server/lib/transaction_supervisor/transaction_participant_provider.h>

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/server/node/cellar_node/dynamic_bundle_config_manager.h>
#include <yt/yt/server/node/cellar_node/config.h>
#include <yt/yt/server/node/cellar_node/master_connector.h>

#include <yt/yt/server/node/cluster_node/bootstrap.h>
#include <yt/yt/server/node/cluster_node/config.h>
#include <yt/yt/server/node/cluster_node/dynamic_config_manager.h>
#include <yt/yt/server/node/cluster_node/master_connector.h>

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/client.h>

#include <yt/yt/ytlib/chunk_client/chunk_fragment_reader.h>

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

#include <yt/yt/client/api/client.h>
#include <yt/yt/client/api/transaction.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/client/security_client/public.h>

#include <yt/yt/client/transaction_client/timestamp_provider.h>

#include <yt/yt/ytlib/hive/cluster_directory_synchronizer.h>

#include <yt/yt/ytlib/tablet_client/config.h>

#include <yt/yt/ytlib/election/cell_manager.h>

#include <yt/yt/core/concurrency/fair_share_action_queue.h>
#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/rpc/response_keeper.h>

#include <yt/yt/core/ytree/virtual.h>
#include <yt/yt/core/ytree/helpers.h>

#include <yt/yt/core/misc/atomic_object.h>

namespace NYT::NTabletNode {

using namespace NApi;
using namespace NCellarAgent;
using namespace NCellarClient;
using namespace NCellarNode;
using namespace NDistributedThrottler;
using namespace NConcurrency;
using namespace NElection;
using namespace NHiveClient;
using namespace NHiveServer;
using namespace NHydra;
using namespace NLeaseServer;
using namespace NObjectClient;
using namespace NRpc;
using namespace NSecurityServer;
using namespace NTabletClient::NProto;
using namespace NTabletClient;
using namespace NTransactionSupervisor;
using namespace NChunkClient;
using namespace NYTree;
using namespace NYson;

using NHydra::EPeerState;

////////////////////////////////////////////////////////////////////////////////

static const TString TabletCellHydraTracker = "TabletCellHydra";

////////////////////////////////////////////////////////////////////////////////

static TThroughputThrottlerConfigPtr GetChangelogThrottlerConfig(
    const NTabletClient::TTabletCellOptionsPtr& tabletCellOptions,
    const TBundleDynamicConfigPtr& bundleConfig)
{
    auto result = New<TThroughputThrottlerConfig>();
    if (!tabletCellOptions || !bundleConfig) {
        return result;
    }

    const auto& mediumThrottlerConfig = bundleConfig->MediumThroughputLimits;

    auto it = mediumThrottlerConfig.find(tabletCellOptions->ChangelogPrimaryMedium);
    if (it != mediumThrottlerConfig.end()) {
        result->Limit = it->second->WriteByteRate;
    } else {
        static constexpr auto UnlimitedThroughput = 1024_TB;
        result->Limit = UnlimitedThroughput;
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

class TSubscriptionGuard final
{
public:
    using TDynamicConfigCallback = TCallback<void(
        const TBundleDynamicConfigPtr& oldConfig,
        const TBundleDynamicConfigPtr& newConfig)>;

    TSubscriptionGuard(
        TBundleDynamicConfigManagerPtr manager,
        TDynamicConfigCallback callback)
        : Manager_(std::move(manager))
        , Callback_(std::move(callback))
    {
        Manager_->SubscribeConfigChanged(Callback_);
    }

    ~TSubscriptionGuard()
    {
        Manager_->UnsubscribeConfigChanged(Callback_);
    }

private:
    TBundleDynamicConfigManagerPtr Manager_;
    TDynamicConfigCallback Callback_;
};

DEFINE_REFCOUNTED_TYPE(TSubscriptionGuard);

////////////////////////////////////////////////////////////////////////////////


class TTabletSlot
    : public TAutomatonInvokerHood<EAutomatonThreadQueue>
    , public ITabletSlot
    , public ITransactionManagerHost
{
private:
    using THood = TAutomatonInvokerHood<EAutomatonThreadQueue>;

public:
    TTabletSlot(
        int slotIndex,
        TTabletNodeConfigPtr config,
        IBootstrap* bootstrap)
        : THood(Format("TabletSlot/%v", slotIndex))
        , Config_(config)
        , Bootstrap_(bootstrap)
        , SnapshotQueue_(New<TActionQueue>(
            Format("TabletSnap/%v", slotIndex)))
        , Logger(TabletNodeLogger)
        , SlotIndex_(slotIndex)
    {
        VERIFY_INVOKER_THREAD_AFFINITY(GetAutomatonInvoker(), AutomatonThread);

        ResetEpochInvokers();
        ResetGuardedInvokers();
    }

    void SetOccupant(ICellarOccupantPtr occupant) override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YT_VERIFY(!Occupant_);

        Occupant_ = std::move(occupant);
        Logger = GetLogger();
    }

    IInvokerPtr GetAutomatonInvoker(EAutomatonThreadQueue queue = EAutomatonThreadQueue::Default) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return THood::GetAutomatonInvoker(queue);
    }

    IInvokerPtr GetOccupierAutomatonInvoker() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return GetAutomatonInvoker(EAutomatonThreadQueue::Default);
    }

    IInvokerPtr GetMutationAutomatonInvoker() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return GetAutomatonInvoker(EAutomatonThreadQueue::Mutation);
    }

    IInvokerPtr GetEpochAutomatonInvoker(EAutomatonThreadQueue queue = EAutomatonThreadQueue::Default) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return THood::GetEpochAutomatonInvoker(queue);
    }

    IInvokerPtr GetGuardedAutomatonInvoker(EAutomatonThreadQueue queue = EAutomatonThreadQueue::Default) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return THood::GetGuardedAutomatonInvoker(queue);
    }

    const IMutationForwarderPtr& GetMutationForwarder() override
    {
        return MutationForwarder_;
    }

    TCellId GetCellId() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Occupant_->GetCellId();
    }

    EPeerState GetAutomatonState() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto hydraManager = GetHydraManager();
        return hydraManager ? hydraManager->GetAutomatonState() : EPeerState::None;
    }

    int GetAutomatonTerm() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto hydraManager = GetHydraManager();
        return hydraManager ? hydraManager->GetAutomatonTerm() : InvalidTerm;
    }

    const TString& GetTabletCellBundleName() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Occupant_->GetCellBundleName();
    }

    IDistributedHydraManagerPtr GetHydraManager() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Occupant_->GetHydraManager();
    }

    ISimpleHydraManagerPtr GetSimpleHydraManager() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Occupant_->GetHydraManager();
    }

    const TCompositeAutomatonPtr& GetAutomaton() override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return Occupant_->GetAutomaton();
    }

    const IHiveManagerPtr& GetHiveManager() override
    {
        return Occupant_->GetHiveManager();
    }

    const TSimpleAvenueDirectoryPtr& GetAvenueDirectory() override
    {
        return Occupant_->GetAvenueDirectory();
    }

    TMailbox* GetMasterMailbox() override
    {
        return Occupant_->GetMasterMailbox();
    }

    void RegisterMasterAvenue(
        TTabletId tabletId,
        TAvenueEndpointId masterEndpointId,
        TPersistentMailboxState&& cookie) override
    {
        auto nodeEndpointId = GetSiblingAvenueEndpointId(masterEndpointId);
        auto masterCellId = Bootstrap_->GetCellId(CellTagFromId(tabletId));

        const auto& hiveManager = GetHiveManager();
        const auto& avenueDirectory = GetAvenueDirectory();

        hiveManager->GetOrCreateCellMailbox(masterCellId);
        avenueDirectory->UpdateEndpoint(masterEndpointId, masterCellId);

        hiveManager->RegisterAvenueEndpoint(nodeEndpointId, std::move(cookie));
    }

    TPersistentMailboxState UnregisterMasterAvenue(
        TAvenueEndpointId masterEndpointId) override
    {
        auto nodeEndpointId = GetSiblingAvenueEndpointId(masterEndpointId);

        GetAvenueDirectory()->UpdateEndpoint(masterEndpointId, /*cellId*/ {});
        return GetHiveManager()->UnregisterAvenueEndpoint(nodeEndpointId);
    }

    void RegisterSiblingTabletAvenue(
        TAvenueEndpointId siblingEndpointId,
        TCellId siblingCellId) override
    {
        auto selfEndpointId = GetSiblingAvenueEndpointId(siblingEndpointId);

        const auto& hiveManager = GetHiveManager();
        const auto& avenueDirectory = GetAvenueDirectory();

        hiveManager->GetOrCreateCellMailbox(siblingCellId);
        avenueDirectory->UpdateEndpoint(siblingEndpointId, siblingCellId);

        hiveManager->RegisterAvenueEndpoint(selfEndpointId, {});
    }

    void UnregisterSiblingTabletAvenue(
        TAvenueEndpointId siblingEndpointId) override
    {
        auto selfEndpointId = GetSiblingAvenueEndpointId(siblingEndpointId);

        GetAvenueDirectory()->UpdateEndpoint(siblingEndpointId, /*cellId*/ {});
        GetHiveManager()->UnregisterAvenueEndpoint(selfEndpointId);
    }

    void CommitTabletMutation(const ::google::protobuf::MessageLite& message) override
    {
        auto mutation = CreateMutation(GetHydraManager(), message);
        GetEpochAutomatonInvoker()->Invoke(BIND([=, this, this_ = MakeStrong(this), mutation = std::move(mutation)] {
            YT_UNUSED_FUTURE(mutation->CommitAndLog(Logger));
        }));
    }

    void PostMasterMessage(TTabletId tabletId, const ::google::protobuf::MessageLite& message) override
    {
        YT_VERIFY(HasMutationContext());

        const auto& hiveManager = GetHiveManager();
        TMailbox* mailbox = hiveManager->GetOrCreateCellMailbox(Bootstrap_->GetCellId(CellTagFromId(tabletId)));
        if (!mailbox) {
            mailbox = GetMasterMailbox();
        }
        hiveManager->PostMessage(mailbox, message);
    }

    const ITransactionManagerPtr& GetTransactionManager() override
    {
        return TransactionManager_;
    }

    const IDistributedThrottlerManagerPtr& GetDistributedThrottlerManager() override
    {
        return DistributedThrottlerManager_;
    }

    NTransactionSupervisor::ITransactionManagerPtr GetOccupierTransactionManager() override
    {
        return GetTransactionManager();
    }

    const ITransactionSupervisorPtr& GetTransactionSupervisor() override
    {
        return Occupant_->GetTransactionSupervisor();
    }

    const ILeaseManagerPtr& GetLeaseManager() override
    {
        return Occupant_->GetLeaseManager();
    }

    ITabletManagerPtr GetTabletManager() override
    {
        return TabletManager_;
    }

    const ITabletCellWriteManagerPtr& GetTabletCellWriteManager() override
    {
        return TabletCellWriteManager_;
    }

    const ISmoothMovementTrackerPtr& GetSmoothMovementTracker() override
    {
        return SmoothMovementTracker_;
    }

    const IHunkTabletManagerPtr& GetHunkTabletManager() override
    {
        return HunkTabletManager_;
    }

    const IResourceLimitsManagerPtr& GetResourceLimitsManager() const override
    {
        return Bootstrap_->GetResourceLimitsManager();
    }

    TObjectId GenerateId(EObjectType type) override
    {
        return Occupant_->GenerateId(type);
    }

    TCompositeAutomatonPtr CreateAutomaton() override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto automation = New<TTabletAutomaton>(
            GetCellId(),
            SnapshotQueue_->GetInvoker(),
            GetLeaseManager());

        if (auto controller = Bootstrap_->GetOverloadController()) {
            automation->RegisterWaitTimeObserver(controller->CreateGenericTracker(
                TabletCellHydraTracker,
                Format("%v.%v", TabletCellHydraTracker, SlotIndex_)));
        } else {
            YT_LOG_WARNING("Failed to register tablet cell hydra wait time tracker for OverloadController (SlotIndex: %v)",
                SlotIndex_);
        }

        return automation;
    }

    TCellTag GetNativeCellTag() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Bootstrap_->GetClient()->GetNativeConnection()->GetPrimaryMasterCellTag();
    }

    const NNative::IConnectionPtr& GetNativeConnection() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Bootstrap_->GetClient()->GetNativeConnection();
    }


    TFuture<TTabletCellMemoryStatistics> GetMemoryStatistics() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return BIND(&TTabletSlot::DoGetMemoryStatistics, MakeStrong(this))
            .AsyncVia(GetAutomatonInvoker())
            .Run();
    }

    TTabletCellMemoryStatistics DoGetMemoryStatistics()
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        return TTabletCellMemoryStatistics{
            .CellId = GetCellId(),
            .BundleName = GetTabletCellBundleName(),
            .Tablets = TabletManager_->GetMemoryStatistics()
        };
    }

    TTimestamp GetLatestTimestamp() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Occupant_
            ->GetTimestampProvider()
            ->GetLatestTimestamp();
    }

    void Configure(IDistributedHydraManagerPtr hydraManager) override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        hydraManager->SubscribeStartLeading(BIND_NO_PROPAGATE(&TTabletSlot::OnStartEpoch, MakeWeak(this)));
        hydraManager->SubscribeStartFollowing(BIND_NO_PROPAGATE(&TTabletSlot::OnStartEpoch, MakeWeak(this)));

        hydraManager->SubscribeStopLeading(BIND_NO_PROPAGATE(&TTabletSlot::OnStopEpoch, MakeWeak(this)));
        hydraManager->SubscribeStopFollowing(BIND_NO_PROPAGATE(&TTabletSlot::OnStopEpoch, MakeWeak(this)));

        InitGuardedInvokers(hydraManager);

        auto mutationForwarderThunk = New<TMutationForwarderThunk>();
        MutationForwarder_ = mutationForwarderThunk;

        // NB: Tablet Manager must register before Transaction Manager since the latter
        // will be writing and deleting rows during snapshot loading.
        TabletManager_ = CreateTabletManager(
            Config_->TabletManager,
            this,
            Bootstrap_);

        {
            auto mutationForwarder = CreateMutationForwarder(
                MakeWeak(TabletManager_),
                GetHiveManager());
            mutationForwarderThunk->SetUnderlying(mutationForwarder);
            MutationForwarder_ = std::move(mutationForwarder);
        }

        HunkTabletManager_ = CreateHunkTabletManager(
            Bootstrap_,
            this);

        TransactionManager_ = CreateTransactionManager(
            Config_->TransactionManager,
            this,
            GetOptions()->ClockClusterTag,
            CreateTransactionLeaseTracker(
                Bootstrap_->GetTransactionTrackerInvoker(),
                Logger));

        DistributedThrottlerManager_ = CreateDistributedThrottlerManager(
            Bootstrap_,
            GetCellId());

        Logger = GetLogger();

        TabletCellWriteManager_ = CreateTabletCellWriteManager(
            TabletManager_->GetTabletCellWriteManagerHost(),
            hydraManager,
            GetAutomaton(),
            GetAutomatonInvoker(),
            GetMutationForwarder());

        SmoothMovementTracker_ = CreateSmoothMovementTracker(
            TabletManager_->GetSmoothMovementTrackerHost(),
            hydraManager,
            GetAutomaton(),
            GetAutomatonInvoker());

        ReconfigureChangelogWriteThrottler();
    }

    void Initialize() override
    {
        TabletService_ = CreateTabletService(
            this,
            Bootstrap_);

        TabletManager_->SubscribeEpochStarted(
            BIND_NO_PROPAGATE(&TTabletSlot::OnTabletsEpochStarted, MakeWeak(this)));
        TabletManager_->SubscribeEpochStopped(
            BIND_NO_PROPAGATE(&TTabletSlot::OnTabletsEpochStopped, MakeWeak(this)));
        TabletManager_->Initialize();
        HunkTabletManager_->Initialize();
        TabletCellWriteManager_->Initialize();
    }

    void RegisterRpcServices() override
    {
        const auto& rpcServer = Bootstrap_->GetRpcServer();
        rpcServer->RegisterService(TabletService_);
    }

    void Stop() override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        const auto& snapshotStore = Bootstrap_->GetTabletSnapshotStore();
        snapshotStore->UnregisterTabletSnapshots(this);

        ResetEpochInvokers();
        ResetGuardedInvokers();
    }

    void Finalize() override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        TabletManager_->Finalize();
        TabletManager_.Reset();

        HunkTabletManager_.Reset();

        TransactionManager_.Reset();
        DistributedThrottlerManager_.Reset();
        TabletCellWriteManager_.Reset();
        SmoothMovementTracker_.Reset();

        if (TabletService_) {
            const auto& rpcServer = Bootstrap_->GetRpcServer();
            rpcServer->UnregisterService(TabletService_);
            TabletService_.Reset();
        }
    }

    ECellarType GetCellarType() override
    {
        return CellarType;
    }

    TCompositeMapServicePtr PopulateOrchidService(TCompositeMapServicePtr orchid) override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return orchid
            ->AddChild("life_stage", IYPathService::FromMethod(
                &ITabletManager::GetTabletCellLifeStage,
                MakeWeak(TabletManager_))
                ->Via(GetAutomatonInvoker()))
            ->AddChild("transactions", TransactionManager_->GetOrchidService())
            ->AddChild("tablets", TabletManager_->GetOrchidService())
            ->AddChild("hunk_tablets", HunkTabletManager_->GetOrchidService());
    }

    const TRuntimeTabletCellDataPtr& GetRuntimeData() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return RuntimeData_;
    }

    double GetUsedCpu(double cpuPerTabletSlot) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return GetDynamicOptions()->CpuPerTabletSlot.value_or(cpuPerTabletSlot);
    }

    TDynamicTabletCellOptionsPtr GetDynamicOptions() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Occupant_->GetDynamicOptions();
    }

    TTabletCellOptionsPtr GetOptions() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Occupant_->GetOptions();
    }

    NProfiling::TProfiler GetProfiler() override
    {
        return TabletNodeProfiler;
    }

    IChunkFragmentReaderPtr CreateChunkFragmentReader(TTablet* tablet) override
    {
        return NChunkClient::CreateChunkFragmentReader(
            tablet->GetSettings().HunkReaderConfig,
            Bootstrap_->GetClient(),
            Bootstrap_->GetHintManager(),
            tablet->GetTableProfiler()->GetProfiler().WithPrefix("/chunk_fragment_reader"),
            [bootstrap = Bootstrap_] (EWorkloadCategory category) -> const IThroughputThrottlerPtr& {
                const auto& dynamicConfigManager = bootstrap->GetDynamicConfigManager();
                const auto& tabletNodeConfig = dynamicConfigManager->GetConfig()->TabletNode;

                if (!tabletNodeConfig->EnableChunkFragmentReaderThrottling) {
                    static const IThroughputThrottlerPtr EmptyThrottler;
                    return EmptyThrottler;
                }

                return bootstrap->GetInThrottler(category);
            });
    }

    int EstimateChangelogMediumBytes(int payload) const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Occupant_->EstimateChangelogMediumBytes(payload);
    }

private:
    const TTabletNodeConfigPtr Config_;
    IBootstrap* const Bootstrap_;

    ICellarOccupantPtr Occupant_;

    const TActionQueuePtr SnapshotQueue_;

    NLogging::TLogger Logger;

    const TRuntimeTabletCellDataPtr RuntimeData_ = New<TRuntimeTabletCellData>();

    IMutationForwarderPtr MutationForwarder_;

    ITabletManagerPtr TabletManager_;

    IHunkTabletManagerPtr HunkTabletManager_;

    ITabletCellWriteManagerPtr TabletCellWriteManager_;

    ISmoothMovementTrackerPtr SmoothMovementTracker_;

    ITransactionManagerPtr TransactionManager_;

    IDistributedThrottlerManagerPtr DistributedThrottlerManager_;

    ITransactionSupervisorPtr TransactionSupervisor_;

    std::atomic<bool> TabletEpochActive_;

    NRpc::IServicePtr TabletService_;

    const int SlotIndex_;
    IReconfigurableThroughputThrottlerPtr ChangelogMediumWriteThrottler_;
    TSubscriptionGuardPtr BundleDynamicConfigSubscription_;


    void OnStartEpoch()
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        InitEpochInvokers(GetHydraManager());
    }

    void OnStopEpoch()
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        ResetEpochInvokers();
    }

    void OnTabletsEpochStarted()
    {
        TabletEpochActive_.store(true);
    }

    void OnTabletsEpochStopped()
    {
        TabletEpochActive_.store(false);
    }

    bool IsTabletEpochActive() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return TabletEpochActive_.load();
    }

    NLogging::TLogger GetLogger() const
    {
        return TabletNodeLogger.WithTag("CellId: %v, PeerId: %v",
            Occupant_->GetCellId(),
            Occupant_->GetPeerId());
    }

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);
    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);

    void ReconfigureChangelogWriteThrottler()
    {
        auto throttlerManager = GetDistributedThrottlerManager();
        auto tabletCellOptions = GetOptions();
        auto bundlePath = Format("//sys/tablet_cell_bundles/%v", GetTabletCellBundleName());
        auto profiler = TabletNodeProfiler.WithPrefix("/distributed_throttlers")
            .WithRequiredTag("tablet_cell_bundle", GetTabletCellBundleName())
            .WithTag("cell_id", ToString(GetCellId()), -1);

        auto getOrCreateThrottler = [=] (const TBundleDynamicConfigPtr& bundleConfig) {
            return throttlerManager->GetOrCreateThrottler(
                bundlePath,
                /*cellTag*/ {},
                GetChangelogThrottlerConfig(tabletCellOptions, bundleConfig),
                "changelog_medium_write",
                EDistributedThrottlerMode::Adaptive,
                WriteThrottlerRpcTimeout,
                /* admitUnlimitedThrottler */ true,
                profiler);
        };

        const auto& dynamicConfigManager = Bootstrap_->GetBundleDynamicConfigManager();
        ChangelogMediumWriteThrottler_ = getOrCreateThrottler(dynamicConfigManager->GetConfig());

        auto configChangeHandler = BIND([reconfigureThrottler = getOrCreateThrottler] (
            const TBundleDynamicConfigPtr& /*oldConfig*/,
            const TBundleDynamicConfigPtr& newConfig) {
                // TODO(capone212): do we really need to reconfigure like this here?
                reconfigureThrottler(newConfig);
            })
            .Via(GetAutomatonInvoker());

        BundleDynamicConfigSubscription_ = New<TSubscriptionGuard>(
            Bootstrap_->GetBundleDynamicConfigManager(),
            std::move(configChangeHandler));
    }

    IReconfigurableThroughputThrottlerPtr GetChangelogMediumWriteThrottler() const override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        return ChangelogMediumWriteThrottler_;
    }
};

ITabletSlotPtr CreateTabletSlot(
    int slotIndex,
    TTabletNodeConfigPtr config,
    IBootstrap* bootstrap)
{
    return New<TTabletSlot>(
        slotIndex,
        std::move(config),
        bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode::NYT
