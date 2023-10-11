
#include <library/cpp/yt/logging/logger.h>

#include <yt/cpp/mapreduce/interface/logging/logger.h>

#include <yt/systest/bootstrap_dataset.h>
#include <yt/systest/config.h>
#include <yt/systest/dataset_operation.h>
#include <yt/systest/map_dataset.h>
#include <yt/systest/reduce_dataset.h>

#include <yt/systest/operation.h>
#include <yt/systest/operation/map.h>
#include <yt/systest/operation/multi_map.h>
#include <yt/systest/operation/reduce.h>

#include <yt/systest/run.h>
#include <yt/systest/runner.h>
#include <yt/systest/sort_dataset.h>
#include <yt/systest/table_dataset.h>
#include <yt/systest/test_home.h>
#include <yt/systest/table.h>

#include <yt/systest/test_program.h>

#include <yt/yt/client/api/config.h>
#include <yt/yt/client/api/rpc_proxy/config.h>
#include <yt/yt/client/api/rpc_proxy/connection.h>

#include <yt/yt/library/auth/auth.h>

#include <yt/yt/library/program/config.h>
#include <yt/yt/library/program/program.h>
#include <yt/yt/library/program/helpers.h>

#include <util/system/env.h>

namespace NYT::NTest {

class TRpcConfig : public TSingletonsConfig
{
};

DEFINE_REFCOUNTED_TYPE(TRpcConfig);

NApi::IClientPtr CreateRpcClient(const TConfig& config) {
    auto proxyAddress = GetEnv("YT_PROXY");
    if (proxyAddress.empty()) {
        THROW_ERROR_EXCEPTION("YT_PROXY environment variable must be set");
    }
    auto connectionConfig = New<NApi::NRpcProxy::TConnectionConfig>();
    connectionConfig->ClusterUrl = proxyAddress;
    connectionConfig->ProxyListUpdatePeriod = TDuration::Seconds(5);

    auto singletonsConfig = New<TRpcConfig>();
    if (!config.Ipv6) {
        singletonsConfig->AddressResolver->EnableIPv4 = true;
        singletonsConfig->AddressResolver->EnableIPv6 = false;
    }
    ConfigureSingletons(singletonsConfig);

    auto connection = NApi::NRpcProxy::CreateConnection(connectionConfig);

    auto token = NAuth::LoadToken();
    if (!token) {
        THROW_ERROR_EXCEPTION("YT_TOKEN environment variable must be set");
    }

    NApi::TClientOptions clientOptions = NAuth::TAuthenticationOptions::FromToken(*token);
    return connection->CreateClient(clientOptions);
}

void RunTestProgram(int argc, char* argv[]) {
    NYT::Initialize();
    NYT::SetLogger(NYT::CreateStdErrLogger(NYT::ILogger::INFO));
    NYT::NLogging::TLogger Logger("test");

    YT_LOG_INFO("Starting tester");

    TConfig config;
    config.ParseCommandline(argc, argv);

    auto client = NYT::CreateClientFromEnv();
    auto rpcClient = CreateRpcClient(config);

    TRunner runner(config.RunnerConfig, client, rpcClient);
    runner.Run();
}

}  // namespace NYT::NTest
