#include "operation_controller.h"
#include "operation_controller_host.h"
#include "config.h"
#include "helpers.h"
#include "operation.h"

#include <yt/yt/library/ytprof/heap_profiler.h>

#include <yt/yt/server/controller_agent/controllers/ordered_controller.h>
#include <yt/yt/server/controller_agent/controllers/sort_controller.h>
#include <yt/yt/server/controller_agent/controllers/sorted_controller.h>
#include <yt/yt/server/controller_agent/controllers/unordered_controller.h>
#include <yt/yt/server/controller_agent/controllers/vanilla_controller.h>

#include <yt/yt/ytlib/object_client/public.h>

#include <yt/yt/ytlib/scheduler/config.h>
#include <yt/yt/ytlib/scheduler/job_resources_helpers.h>
#include <yt/yt/ytlib/scheduler/proto/job.pb.h>

#include <yt/yt/core/tracing/trace_context.h>

#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/yson/consumer.h>
#include <yt/yt/core/yson/string.h>

namespace NYT::NControllerAgent {

using namespace NApi;
using namespace NScheduler;
using namespace NObjectClient;
using namespace NProfiling;
using namespace NTracing;
using namespace NYson;
using namespace NYPath;
using namespace NYTree;
using namespace NYTAlloc;
using namespace NYTProf;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TControllerTransactionIds* transactionIdsProto, const NControllerAgent::TControllerTransactionIds& transactionIds)
{
    ToProto(transactionIdsProto->mutable_async_id(), transactionIds.AsyncId);
    ToProto(transactionIdsProto->mutable_input_id(), transactionIds.InputId);
    ToProto(transactionIdsProto->mutable_output_id(), transactionIds.OutputId);
    ToProto(transactionIdsProto->mutable_debug_id(), transactionIds.DebugId);
    ToProto(transactionIdsProto->mutable_output_completion_id(), transactionIds.OutputCompletionId);
    ToProto(transactionIdsProto->mutable_debug_completion_id(), transactionIds.DebugCompletionId);
    ToProto(transactionIdsProto->mutable_nested_input_ids(), transactionIds.NestedInputIds);
}

void FromProto(NControllerAgent::TControllerTransactionIds* transactionIds, const NProto::TControllerTransactionIds& transactionIdsProto)
{
    transactionIds->AsyncId = FromProto<TTransactionId>(transactionIdsProto.async_id());
    transactionIds->InputId = FromProto<TTransactionId>(transactionIdsProto.input_id());
    transactionIds->OutputId = FromProto<TTransactionId>(transactionIdsProto.output_id());
    transactionIds->DebugId  = FromProto<TTransactionId>(transactionIdsProto.debug_id());
    transactionIds->OutputCompletionId = FromProto<TTransactionId>(transactionIdsProto.output_completion_id());
    transactionIds->DebugCompletionId = FromProto<TTransactionId>(transactionIdsProto.debug_completion_id());
    transactionIds->NestedInputIds = FromProto<std::vector<TTransactionId>>(transactionIdsProto.nested_input_ids());
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TInitializeOperationResult* resultProto, const TOperationControllerInitializeResult& result)
{
    resultProto->set_mutable_attributes(result.Attributes.Mutable.ToString());
    resultProto->set_brief_spec(result.Attributes.BriefSpec.ToString());
    resultProto->set_full_spec(result.Attributes.FullSpec.ToString());
    resultProto->set_unrecognized_spec(result.Attributes.UnrecognizedSpec.ToString());
    ToProto(resultProto->mutable_transaction_ids(), result.TransactionIds);
    resultProto->set_erase_offloading_trees(result.EraseOffloadingTrees);
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TPrepareOperationResult* resultProto, const TOperationControllerPrepareResult& result)
{
    if (result.Attributes) {
        resultProto->set_attributes(result.Attributes.ToString());
    }
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TMaterializeOperationResult* resultProto, const TOperationControllerMaterializeResult& result)
{
    resultProto->set_suspend(result.Suspend);
    ToProto(resultProto->mutable_initial_composite_needed_resources(), result.InitialNeededResources);
    ToProto(resultProto->mutable_initial_min_needed_resources(), result.InitialMinNeededResources);
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TReviveOperationResult* resultProto, const TOperationControllerReviveResult& result)
{
    resultProto->set_attributes(result.Attributes.ToString());
    resultProto->set_revived_from_snapshot(result.RevivedFromSnapshot);
    for (const auto& job : result.RevivedJobs) {
        auto* jobProto = resultProto->add_revived_jobs();
        ToProto(jobProto->mutable_job_id(), job.JobId);
        jobProto->set_start_time(ToProto<ui64>(job.StartTime));
        ToProto(jobProto->mutable_resource_limits(), job.ResourceLimits);
        ToProto(jobProto->mutable_disk_quota(), job.DiskQuota);
        jobProto->set_interruptible(job.Interruptible);
        jobProto->set_tree_id(job.TreeId);
        jobProto->set_node_id(ToProto<ui32>(job.NodeId));
        jobProto->set_node_address(job.NodeAddress);
    }
    ToProto(resultProto->mutable_revived_banned_tree_ids(), result.RevivedBannedTreeIds);
    ToProto(resultProto->mutable_composite_needed_resources(), result.NeededResources);
    ToProto(resultProto->mutable_min_needed_resources(), result.MinNeededResources);
    ToProto(resultProto->mutable_initial_min_needed_resources(), result.InitialMinNeededResources);
    resultProto->set_controller_epoch(result.ControllerEpoch);
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TCommitOperationResult* /* resultProto */, const TOperationControllerCommitResult& /* result */)
{ }

////////////////////////////////////////////////////////////////////////////////

void ToProto(
    NScheduler::NProto::TAgentToSchedulerRunningJobStatistics* jobStatisticsProto,
    const TAgentToSchedulerRunningJobStatistics& jobStatistics)
{
    ToProto(jobStatisticsProto->mutable_job_id(), jobStatistics.JobId);

    jobStatisticsProto->set_preemptible_progress_time(ToProto<i64>(jobStatistics.PreemptibleProgressTime));
}

////////////////////////////////////////////////////////////////////////////////

//! Ensures that operation controllers are being destroyed in a
//! dedicated invoker and releases memory tag when controller is destroyed.
class TOperationControllerWrapper
    : public IOperationController
{
private:
    template<typename Class, typename R, typename... MArgs, typename... Args>
    decltype(auto) DoExecuteGuarded(R(Class::*Method)(MArgs...) const, Args&&... args)
        const
        noexcept(false)
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);
        auto testingHeap = Underlying_->TestHeap();

        return (*Underlying_.*Method)(std::forward<MArgs>(args)...);
    }

    template<typename Class, typename R, typename... MArgs, typename... Args>
    decltype(auto) DoExecuteGuarded(R(Class::*Method)(MArgs...), Args&&... args)
        noexcept(false)
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);
        auto testingHeap = Underlying_->TestHeap();

        return (*Underlying_.*Method)(std::forward<MArgs>(args)...);
    }

public:
    TOperationControllerWrapper(
        TOperationId id,
        IOperationControllerPtr underlying,
        IInvokerPtr dtorInvoker,
        TTraceContext* parentTraceContext)
        : Id_(id)
        , Underlying_(std::move(underlying))
        , DtorInvoker_(std::move(dtorInvoker))
        , TraceContext_(
            parentTraceContext
            ? parentTraceContext->CreateChild("OperationControllerWrapper")
            : CreateTraceContextFromCurrent("OperationControllerWrapper"))
        , TraceContextFinishGuard_(TraceContext_)
    {
        TraceContext_->SetAllocationTags({{OperationIdAllocationTag, ToString(Id_)}});
    }

    ~TOperationControllerWrapper() override
    {
        auto Logger = ControllerLogger.WithTag("OperationId: %v", Id_);

        YT_LOG_INFO("Controller wrapper destructed, controller destruction scheduled (MemoryUsage: %v)",
            GetMemoryUsageSnapshot()->GetUsage(OperationIdAllocationTag, ToString(Id_)));

        DtorInvoker_->Invoke(BIND([
            underlying = std::move(Underlying_),
            id = Id_,
            Logger] () mutable
        {
            NProfiling::TWallTimer timer;
            auto memoryUsageBefore = NYTProf::GetMemoryUsageSnapshot()->GetUsage(OperationIdAllocationTag, ToString(id));
            YT_LOG_INFO("Started destructing operation controller (MemoryUsageBefore: %v)", memoryUsageBefore);
            if (auto refCount = ResetAndGetResidualRefCount(underlying)) {
                YT_LOG_WARNING(
                    "Controller is going to be removed, but it has residual reference count; memory leak is possible "
                    "(RefCount: %v)",
                    refCount);
            }
            auto memoryUsageAfter = NYTProf::GetMemoryUsageSnapshot()->GetUsage(OperationIdAllocationTag, ToString(id));
            YT_LOG_INFO("Finished destructing operation controller (Elapsed: %v, MemoryUsageAfter: %v, MemoryUsageDecrease: %v)",
                timer.GetElapsedTime(),
                memoryUsageAfter,
                memoryUsageBefore - memoryUsageAfter);
        }));
    }

    std::vector<TTestAllocGuard> TestHeap() const override
    {
        return Underlying_->TestHeap();
    }

    std::pair<NApi::ITransactionPtr, TString> GetIntermediateMediumTransaction() override
    {
        return DoExecuteGuarded(&IOperationController::GetIntermediateMediumTransaction);
    }

    void UpdateIntermediateMediumUsage(i64 usage) override
    {
        return DoExecuteGuarded(&IOperationController::UpdateIntermediateMediumUsage, usage);
    }

    TOperationControllerInitializeResult InitializeClean() override
    {
        return DoExecuteGuarded(&IOperationController::InitializeClean);
    }

    TOperationControllerInitializeResult InitializeReviving(const TControllerTransactionIds& transactions) override
    {
        return DoExecuteGuarded(&IOperationController::InitializeReviving, transactions);
    }

    TOperationControllerPrepareResult Prepare() override
    {
        return DoExecuteGuarded(&IOperationController::Prepare);
    }

    TOperationControllerMaterializeResult Materialize() override
    {
        return DoExecuteGuarded(&IOperationController::Materialize);
    }

    void Commit() override
    {
        return DoExecuteGuarded(&IOperationController::Commit);
    }

    void SaveSnapshot(IZeroCopyOutput* output) override
    {
        return DoExecuteGuarded(&IOperationController::SaveSnapshot, output);
    }

    TOperationControllerReviveResult Revive() override
    {
        return DoExecuteGuarded(&IOperationController::Revive);
    }

    void Terminate(EControllerState finalState) override
    {
        return Underlying_->Terminate(finalState);
    }

    void Cancel() override
    {
        return Underlying_->Cancel();
    }

    void Complete() override
    {
        return Underlying_->Complete();
    }

    void Dispose() override
    {
        return Underlying_->Dispose();
    }

    bool IsThrottling() const noexcept override
    {
        return Underlying_->IsThrottling();
    }

    void RecordScheduleJobFailure(EScheduleJobFailReason reason) noexcept override
    {
        return Underlying_->RecordScheduleJobFailure(reason);
    }

    void UpdateRuntimeParameters(const TOperationRuntimeParametersUpdatePtr& update) override
    {
        return DoExecuteGuarded(&IOperationController::UpdateRuntimeParameters, update);
    }

    void OnTransactionsAborted(const std::vector<TTransactionId>& transactionIds) override
    {
        return DoExecuteGuarded(&IOperationController::OnTransactionsAborted, transactionIds);
    }

    TCancelableContextPtr GetCancelableContext() const override
    {
        return DoExecuteGuarded(&IOperationController::GetCancelableContext);
    }

    IInvokerPtr GetInvoker(EOperationControllerQueue queue) const override
    {
        return DoExecuteGuarded(&IOperationController::GetInvoker, queue);
    }

    IInvokerPtr GetCancelableInvoker(EOperationControllerQueue queue) const override
    {
        return DoExecuteGuarded(&IOperationController::GetCancelableInvoker, queue);
    }

    IDiagnosableInvokerPool::TInvokerStatistics GetInvokerStatistics(EOperationControllerQueue queue) const override
    {
        return DoExecuteGuarded(&IOperationController::GetInvokerStatistics, queue);
    }

    TFuture<void> Suspend() override
    {
        return DoExecuteGuarded(&IOperationController::Suspend);
    }

    void Resume() override
    {
        return DoExecuteGuarded(&IOperationController::Resume);
    }

    TCompositePendingJobCount GetPendingJobCount() const override
    {
        return DoExecuteGuarded(&IOperationController::GetPendingJobCount);
    }

    i64 GetFailedJobCount() const override
    {
        return Underlying_->GetFailedJobCount();
    }

    bool ShouldUpdateLightOperationAttributes() const override
    {
        return Underlying_->ShouldUpdateLightOperationAttributes();
    }

    void SetLightOperationAttributesUpdated() override
    {
        return Underlying_->SetLightOperationAttributesUpdated();
    }

    bool IsRunning() const override
    {
        return Underlying_->IsRunning();
    }

    TCompositeNeededResources GetNeededResources() const override
    {
        return DoExecuteGuarded(&IOperationController::GetNeededResources);
    }

    void UpdateMinNeededJobResources() override
    {
        return DoExecuteGuarded(&IOperationController::UpdateMinNeededJobResources);
    }

    TJobResourcesWithQuotaList GetMinNeededJobResources() const override
    {
        return DoExecuteGuarded(&IOperationController::GetMinNeededJobResources);
    }

    void OnJobAbortedEventReceivedFromScheduler(TAbortedBySchedulerJobSummary&& eventSummary) override
    {
        return DoExecuteGuarded(&IOperationControllerSchedulerHost::OnJobAbortedEventReceivedFromScheduler, std::move(eventSummary));
    }

    void AbandonJob(TJobId jobId) override
    {
        return DoExecuteGuarded(&IOperationController::AbandonJob, std::move(jobId));
    }

    void OnJobInfoReceivedFromNode(std::unique_ptr<TJobSummary> jobSummary) override
    {
        return DoExecuteGuarded(&IOperationController::OnJobInfoReceivedFromNode, std::move(jobSummary));
    }

    void AbortJobByJobTracker(TJobId jobId, EAbortReason abortReason) override
    {
        return DoExecuteGuarded(&IOperationController::AbortJobByJobTracker, std::move(jobId), abortReason);
    }

    TControllerScheduleJobResultPtr ScheduleJob(
        ISchedulingContext* context,
        const TJobResources& jobLimits,
        const TString& treeId) override
    {
        return DoExecuteGuarded(&IOperationController::ScheduleJob, context, jobLimits, treeId);
    }

    void UpdateConfig(const TControllerAgentConfigPtr& config) override
    {
        return DoExecuteGuarded(&IOperationController::UpdateConfig, config);
    }

    bool ShouldUpdateProgressAttributes() const override
    {
        return Underlying_->ShouldUpdateProgressAttributes();
    }

    void SetProgressAttributesUpdated() override
    {
        return Underlying_->SetProgressAttributesUpdated();
    }

    bool HasProgress() const override
    {
        return Underlying_->HasProgress();
    }

    TYsonString GetProgress() const override
    {
        return DoExecuteGuarded(&IOperationController::GetProgress);
    }

    TYsonString GetBriefProgress() const override
    {
        return DoExecuteGuarded(&IOperationController::GetBriefProgress);
    }

    TYsonString BuildJobYson(TJobId jobId, bool outputStatistics) const override
    {
        return DoExecuteGuarded(&IOperationController::BuildJobYson, std::move(jobId), outputStatistics);
    }

    TJobStartInfo SettleJob(TAllocationId allocationId) override
    {
        return DoExecuteGuarded(&IOperationController::SettleJob, std::move(allocationId));
    }

    TOperationJobMetrics PullJobMetricsDelta(bool force) override
    {
        return DoExecuteGuarded(&IOperationController::PullJobMetricsDelta, force);
    }

    TOperationAlertMap GetAlerts() override
    {
        return DoExecuteGuarded(&IOperationController::GetAlerts);
    }

    TOperationInfo BuildOperationInfo() override
    {
        return DoExecuteGuarded(&IOperationController::BuildOperationInfo);
    }

    TYsonString GetSuspiciousJobsYson() const override
    {
        return DoExecuteGuarded(&IOperationController::GetSuspiciousJobsYson);
    }

    TSnapshotCookie OnSnapshotStarted() override
    {
        return DoExecuteGuarded(&IOperationController::OnSnapshotStarted);
    }

    void OnSnapshotCompleted(const TSnapshotCookie& cookie) override
    {
        return DoExecuteGuarded(&IOperationController::OnSnapshotCompleted, cookie);
    }

    bool HasSnapshot() const override
    {
        return Underlying_->HasSnapshot();
    }

    IYPathServicePtr GetOrchid() const override
    {
        return DoExecuteGuarded(&IOperationController::GetOrchid);
    }

    void ZombifyOrchid() override
    {
        return DoExecuteGuarded(&IOperationController::ZombifyOrchid);
    }

    TString WriteCoreDump() const override
    {
        return DoExecuteGuarded(&IOperationController::WriteCoreDump);
    }

    void RegisterOutputRows(i64 count, int tableIndex) override
    {
        return DoExecuteGuarded(&IOperationController::RegisterOutputRows, count, tableIndex);
    }

    std::optional<int> GetRowCountLimitTableIndex() override
    {
        return Underlying_->GetRowCountLimitTableIndex();
    }

    void LoadSnapshot(const TOperationSnapshot& snapshot) override
    {
        return DoExecuteGuarded(&IOperationController::LoadSnapshot, snapshot);
    }

    i64 GetMemoryUsage() const override
    {
        return Underlying_->GetMemoryUsage();
    }

    void SetOperationAlert(EOperationAlertType type, const TError& alert) override
    {
        return DoExecuteGuarded(&IOperationController::SetOperationAlert, type, alert);
    }

    void OnMemoryLimitExceeded(const TError& error) override
    {
        return DoExecuteGuarded(&IOperationController::OnMemoryLimitExceeded, error);
    }

    bool IsMemoryLimitExceeded() const override
    {
        return Underlying_->IsMemoryLimitExceeded();
    }

    bool IsFinished() const override
    {
        return Underlying_->IsFinished();
    }

private:
    const TOperationId Id_;
    const IOperationControllerPtr Underlying_;
    const IInvokerPtr DtorInvoker_;

    const TTraceContextPtr TraceContext_;
    const TTraceContextFinishGuard TraceContextFinishGuard_;
};

////////////////////////////////////////////////////////////////////////////////

void ApplyPatch(
    const TYPath& path,
    const INodePtr& root,
    const INodePtr& templatePatch,
    const INodePtr& patch)
{
    auto node = FindNodeByYPath(root, path);
    if (node) {
        node = CloneNode(node);
    }
    if (templatePatch) {
        if (node) {
            node = PatchNode(templatePatch, node);
        } else {
            node = templatePatch;
        }
    }
    if (patch) {
        if (node) {
            node = PatchNode(node, patch);
        } else {
            node = patch;
        }
    }
    if (node) {
        ForceYPath(root, path);
        // Note that #node may be equal to one of the #root's subtrees or to one of the patches.
        // In any case, we do not want to use it as an argument to SetNodeByYPath, since this wonderful
        // method would change the parent of the argument node, which may lead to child-parent relation inconsistency.
        SetNodeByYPath(root, path, CloneNode(node));
    }
}

void ApplyExperiments(TOperation* operation)
{
    const auto& spec = operation->GetSpec();
    std::vector<TYPath> userJobPaths;
    std::vector<TYPath> jobIOPaths;
    jobIOPaths.push_back("/auto_merge/job_io");
    switch (operation->GetType()) {
        case EOperationType::Map: {
            userJobPaths.push_back("/mapper");
            jobIOPaths.push_back("/job_io");
            break;
        }
        case EOperationType::JoinReduce:
        case EOperationType::Reduce: {
            userJobPaths.push_back("/reducer");
            jobIOPaths.push_back("/job_io");
            break;
        }
        case EOperationType::MapReduce: {
            if (FindNodeByYPath(spec, "/mapper")) {
                userJobPaths.push_back("/mapper");
            }
            if (FindNodeByYPath(spec, "/reduce_combiner")) {
                userJobPaths.push_back("/reduce_combiner");
            }
            userJobPaths.push_back("/reducer");
            jobIOPaths.push_back("/map_job_io");
            jobIOPaths.push_back("/sort_job_io");
            jobIOPaths.push_back("/reduce_job_io");
            break;
        }
        case EOperationType::Sort: {
            jobIOPaths.push_back("/partition_job_io");
            jobIOPaths.push_back("/sort_job_io");
            jobIOPaths.push_back("/merge_job_io");
            break;
        }
        case EOperationType::Merge:
        case EOperationType::Erase:
        case EOperationType::RemoteCopy: {
            jobIOPaths.push_back("/job_io");
            break;
        }
        case EOperationType::Vanilla: {
            auto tasks = GetNodeByYPath(spec, "/tasks");
            for (const auto& key : tasks->AsMap()->GetKeys()) {
                userJobPaths.push_back("/tasks/" + key);
                jobIOPaths.push_back("/tasks/" + key + "/job_io");
            }
            break;
        }
    }

    for (const auto& experiment : operation->ExperimentAssignments()) {
        for (const auto& path : userJobPaths) {
            ApplyPatch(
                path,
                spec,
                experiment->Effect->ControllerUserJobSpecTemplatePatch,
                experiment->Effect->ControllerUserJobSpecPatch);
        }
        for (const auto& path : jobIOPaths) {
            ApplyPatch(
                path,
                spec,
                experiment->Effect->ControllerJobIOTemplatePatch,
                experiment->Effect->ControllerJobIOPatch);
        }
    }
}

IOperationControllerPtr CreateControllerForOperation(
    TControllerAgentConfigPtr config,
    TOperation* operation,
    TTraceContext* parentTraceContext)
{
    IOperationControllerPtr controller;
    auto host = operation->GetHost();
    ApplyExperiments(operation);
    switch (operation->GetType()) {
        case EOperationType::Map: {
            auto baseSpec = ParseOperationSpec<TMapOperationSpec>(operation->GetSpec());
            controller = baseSpec->Ordered
                ? NControllers::CreateOrderedMapController(config, host, operation)
                : NControllers::CreateUnorderedMapController(config, host, operation);
            break;
        }
        case EOperationType::Merge: {
            auto baseSpec = ParseOperationSpec<TMergeOperationSpec>(operation->GetSpec());
            switch (baseSpec->Mode) {
                case EMergeMode::Ordered: {
                    controller = NControllers::CreateOrderedMergeController(config, host, operation);
                    break;
                }
                case EMergeMode::Sorted: {
                    controller = NControllers::CreateSortedMergeController(config, host, operation);
                    break;
                }
                case EMergeMode::Unordered: {
                    controller = NControllers::CreateUnorderedMergeController(config, host, operation);
                    break;
                }
            }
            break;
        }
        case EOperationType::Erase: {
            controller = NControllers::CreateEraseController(config, host, operation);
            break;
        }
        case EOperationType::Sort: {
            controller = NControllers::CreateSortController(config, host, operation);
            break;
        }
        case EOperationType::Reduce: {
            controller = NControllers::CreateReduceController(config, host, operation, /* isJoinReduce */ false);
            break;
        }
        case EOperationType::JoinReduce: {
            controller = NControllers::CreateReduceController(config, host, operation, /* isJoinReduce */ true);
            break;
        }
        case EOperationType::MapReduce: {
            controller = NControllers::CreateMapReduceController(config, host, operation);
            break;
        }
        case EOperationType::RemoteCopy: {
            controller = NControllers::CreateRemoteCopyController(config, host, operation);
            break;
        }
        case EOperationType::Vanilla: {
            controller = NControllers::CreateVanillaController(config, host, operation);
            break;
        }
        default:
            YT_ABORT();
    }

    return New<TOperationControllerWrapper>(
        operation->GetId(),
        controller,
        controller->GetInvoker(),
        parentTraceContext);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
