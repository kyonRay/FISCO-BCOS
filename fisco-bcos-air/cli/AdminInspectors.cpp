#include "AdminInspectors.h"
#include "FallbackInspectors.h"
#include <future>

namespace
{
using namespace std::chrono_literals;

bcos::air::cli::InspectSection makeUnavailableSection(const std::string& reason)
{
    bcos::air::cli::InspectSection section;
    section.available = false;
    section.reason = reason;
    return section;
}

template <class T>
std::optional<T> waitForFuture(std::future<T>& future, int timeoutMs)
{
    if (future.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready)
    {
        return std::nullopt;
    }
    return future.get();
}

struct LedgerSummary
{
    bool ok = false;
    std::string error;
    int64_t totalTx = 0;
    int64_t failedTx = 0;
    bcos::protocol::BlockNumber latestBlock = -1;
};

struct PendingTxSummary
{
    bool ok = false;
    std::string error;
    uint64_t pendingTxs = 0;
};

LedgerSummary fetchLedgerSummary(const bcos::ledger::LedgerInterface::Ptr& ledger, int timeoutMs)
{
    if (!ledger)
    {
        return {.error = "ledger is not available"};
    }

    auto promise = std::make_shared<std::promise<LedgerSummary>>();
    auto future = promise->get_future();
    ledger->asyncGetTotalTransactionCount(
        [promise](bcos::Error::Ptr error, int64_t totalTx, int64_t failedTx,
            bcos::protocol::BlockNumber latestBlock) mutable {
            LedgerSummary summary;
            if (error)
            {
                summary.error = error->errorMessage();
            }
            else
            {
                summary.ok = true;
                summary.totalTx = totalTx;
                summary.failedTx = failedTx;
                summary.latestBlock = latestBlock;
            }
            promise->set_value(std::move(summary));
        });

    auto result = waitForFuture(future, timeoutMs);
    if (!result)
    {
        return {.error = "timed out while reading ledger summary"};
    }
    return *result;
}

PendingTxSummary fetchPendingTxSummary(
    const bcos::txpool::TxPoolInterface::Ptr& txpool, int timeoutMs)
{
    if (!txpool)
    {
        return {.error = "txpool is not available"};
    }

    auto promise = std::make_shared<std::promise<PendingTxSummary>>();
    auto future = promise->get_future();
    txpool->asyncGetPendingTransactionSize(
        [promise](bcos::Error::Ptr error, uint64_t pendingTxs) mutable {
            PendingTxSummary summary;
            if (error)
            {
                summary.error = error->errorMessage();
            }
            else
            {
                summary.ok = true;
                summary.pendingTxs = pendingTxs;
            }
            promise->set_value(std::move(summary));
        });

    auto result = waitForFuture(future, timeoutMs);
    if (!result)
    {
        return {.error = "timed out while reading txpool summary"};
    }
    return *result;
}

Json::Value buildNodeData(const bcos::tool::NodeConfig::Ptr& nodeConfig,
    const bcos::group::ChainNodeInfo::Ptr& nodeInfo, bool hasRPC, bool hasGateway)
{
    Json::Value data(Json::objectValue);
    data["chainID"] = nodeConfig->chainId();
    data["groupID"] = nodeConfig->groupId();
    data["nodeName"] = nodeConfig->nodeName();
    data["nodeType"] = nodeInfo ? static_cast<int>(nodeInfo->nodeType()) : 0;
    data["nodeID"] = nodeInfo ? nodeInfo->nodeID() : "";
    data["smCryptoType"] = nodeConfig->smCryptoType();
    data["wasm"] = nodeConfig->isWasm();
    data["authCheck"] = nodeConfig->isAuthCheck();
    data["withoutTarsFramework"] = nodeConfig->withoutTarsFramework();
    data["compatibilityVersion"] = nodeConfig->compatibilityVersionStr();
    data["rpcService"] = nodeConfig->rpcServiceName();
    data["gatewayService"] = nodeConfig->gatewayServiceName();
    data["schedulerService"] = nodeConfig->schedulerServiceName();
    data["executorService"] = nodeConfig->executorServiceName();
    data["txpoolService"] = nodeConfig->txpoolServiceName();
    data["rpcObject"] = hasRPC;
    data["gatewayObject"] = hasGateway;
    return data;
}

Json::Value buildChainData(const bcos::tool::NodeConfig::Ptr& nodeConfig,
    const LedgerSummary& ledgerSummary, const PendingTxSummary& pendingSummary)
{
    Json::Value data(Json::objectValue);
    data["chainID"] = nodeConfig->chainId();
    data["groupID"] = nodeConfig->groupId();
    data["consensusType"] = nodeConfig->consensusType();
    data["blockLimit"] = Json::UInt64(nodeConfig->blockLimit());
    data["epochSealerNum"] = Json::Int64(nodeConfig->epochSealerNum());
    data["epochBlockNum"] = Json::Int64(nodeConfig->epochBlockNum());
    if (ledgerSummary.ok)
    {
        data["latestBlock"] = Json::Int64(ledgerSummary.latestBlock);
        data["totalTx"] = Json::Int64(ledgerSummary.totalTx);
        data["failedTx"] = Json::Int64(ledgerSummary.failedTx);
    }
    if (pendingSummary.ok)
    {
        data["pendingTxs"] = Json::UInt64(pendingSummary.pendingTxs);
    }
    return data;
}

Json::Value buildNetworkData(const bcos::tool::NodeConfig::Ptr& nodeConfig,
    const bcos::group::GroupInfo::Ptr& groupInfo,
    const bcos::gateway::GroupNodeInfo::Ptr& groupNodeInfo)
{
    Json::Value data(Json::objectValue);
    data["rpcListenIP"] = nodeConfig->rpcListenIP();
    data["rpcListenPort"] = nodeConfig->rpcListenPort();
    data["web3Enabled"] = nodeConfig->enableWeb3Rpc();
    data["web3ListenIP"] = nodeConfig->web3RpcListenIP();
    data["web3ListenPort"] = nodeConfig->web3RpcListenPort();
    data["p2pListenIP"] = nodeConfig->p2pListenIP();
    data["p2pListenPort"] = nodeConfig->p2pListenPort();
    data["gatewayService"] = nodeConfig->gatewayServiceName();
    data["rpcService"] = nodeConfig->rpcServiceName();
    data["groupInfoNodeCount"] = Json::Int64(groupInfo ? groupInfo->nodesNum() : 0);
    data["connectedNodeCount"] =
        Json::UInt64(groupNodeInfo ? groupNodeInfo->nodeIDList().size() : 0);

    Json::Value connectedNodes(Json::arrayValue);
    if (groupNodeInfo)
    {
        for (const auto& nodeID : groupNodeInfo->nodeIDList())
        {
            connectedNodes.append(nodeID);
        }
    }
    data["connectedNodes"] = std::move(connectedNodes);
    return data;
}

Json::Value buildStorageData(const bcos::tool::NodeConfig::Ptr& nodeConfig)
{
    Json::Value data(Json::objectValue);
    data["type"] = nodeConfig->storageType();
    data["dataPath"] = nodeConfig->storagePath();
    data["stateDBPath"] = nodeConfig->stateDBPath();
    data["blockDBPath"] = nodeConfig->blockDBPath();
    data["enableSeparateBlockAndState"] = nodeConfig->enableSeparateBlockAndState();
    data["enableArchive"] = nodeConfig->enableArchive();
    data["syncArchivedBlocks"] = nodeConfig->syncArchivedBlocks();
    data["storageSecurityEnable"] = nodeConfig->storageSecurityEnable();
    return data;
}

Json::Value buildExecutorData(
    const bcos::tool::NodeConfig::Ptr& nodeConfig, bcos::initializer::Initializer& initializer)
{
    Json::Value data(Json::objectValue);
    data["schedulerAvailable"] = static_cast<bool>(initializer.scheduler());
    data["storageAvailable"] = static_cast<bool>(initializer.storage());
    data["ledgerAvailable"] = static_cast<bool>(initializer.ledger());
    data["serialExecute"] = nodeConfig->isSerialExecute();
    data["vmCacheSize"] = Json::UInt64(nodeConfig->vmCacheSize());
    data["executorService"] = nodeConfig->executorServiceName();
    data["baselineSchedulerParallel"] = nodeConfig->baselineSchedulerConfig().parallel;
    data["baselineSchedulerGrainSize"] = nodeConfig->baselineSchedulerConfig().grainSize;
    data["baselineSchedulerMaxThread"] = nodeConfig->baselineSchedulerConfig().maxThread;
    return data;
}
}  // namespace

namespace bcos::air::cli
{
InspectResponse buildAdminInspectResponse(const AdminInspectRequest& request,
    const InspectConfig& config, bcos::initializer::Initializer& initializer,
    const bcos::rpc::RPCInterface::Ptr& rpc, const bcos::gateway::GatewayInterface::Ptr& gateway)
{
    InspectResponse response;
    response.source = "admin-ipc";
    response.command = request.command;
    response.domain = request.domain;

    auto nodeConfig = initializer.nodeConfig();
    if (!nodeConfig)
    {
        response.node = makeUnavailableSection("node config is not available");
        response.chain = makeUnavailableSection("node config is not available");
        response.network = makeUnavailableSection("node config is not available");
        response.storage = makeUnavailableSection("node config is not available");
        response.executor = makeUnavailableSection("node config is not available");
        response.logs = buildFallbackLogSection(config, request.tail, request.logLevel);
        response.summary.latestBlock = "unavailable";
        response.summary.totalTx = "unavailable";
        response.warnings.emplace_back("admin-ipc handler is running before node config is ready");
        return response;
    }

    auto pbftInitializer = initializer.pbftInitializer();
    auto groupInfo = pbftInitializer ? pbftInitializer->groupInfo() : nullptr;
    auto nodeInfo = pbftInitializer ? pbftInitializer->nodeInfo() : nullptr;
    auto frontInitializer = initializer.frontService();
    auto front = frontInitializer ? frontInitializer->front() : nullptr;
    auto groupNodeInfo = front ? front->groupNodeInfo() : nullptr;
    auto txPoolInitializer = initializer.txPoolInitializer();
    auto txpool = txPoolInitializer ? txPoolInitializer->txpool() : nullptr;

    auto ledgerSummary = fetchLedgerSummary(initializer.ledger(), request.timeoutMs);
    auto pendingSummary = fetchPendingTxSummary(txpool, request.timeoutMs);

    response.summary.latestBlock =
        ledgerSummary.ok ? std::to_string(ledgerSummary.latestBlock) : "unavailable";
    response.summary.totalTx =
        ledgerSummary.ok ? std::to_string(ledgerSummary.totalTx) : "unavailable";

    response.node.available = true;
    response.node.data =
        buildNodeData(nodeConfig, nodeInfo, static_cast<bool>(rpc), static_cast<bool>(gateway));

    if (ledgerSummary.ok)
    {
        response.chain.available = true;
        response.chain.data = buildChainData(nodeConfig, ledgerSummary, pendingSummary);
    }
    else
    {
        response.chain = makeUnavailableSection(ledgerSummary.error);
        response.chain.data = buildChainData(nodeConfig, ledgerSummary, pendingSummary);
        response.warnings.emplace_back("chain summary unavailable: " + ledgerSummary.error);
    }

    response.network.available = true;
    response.network.data = buildNetworkData(nodeConfig, groupInfo, groupNodeInfo);
    if (!groupNodeInfo)
    {
        response.network.reason = "peer topology cache is not populated yet";
    }

    response.storage.available = true;
    response.storage.data = buildStorageData(nodeConfig);

    response.executor.available =
        static_cast<bool>(initializer.scheduler()) || static_cast<bool>(initializer.storage());
    response.executor.reason =
        response.executor.available ? "" : "scheduler and storage are not available";
    response.executor.data = buildExecutorData(nodeConfig, initializer);

    response.logs = buildFallbackLogSection(config, request.tail, request.logLevel);

    if (!pendingSummary.ok)
    {
        response.warnings.emplace_back(
            "pending transaction summary unavailable: " + pendingSummary.error);
    }

    return response;
}

AdminInspectReply buildAdminInspectReply(const AdminInspectRequest& request,
    const InspectConfig& config, bcos::initializer::Initializer& initializer,
    const bcos::rpc::RPCInterface::Ptr& rpc, const bcos::gateway::GatewayInterface::Ptr& gateway)
{
    AdminInspectReply reply;
    try
    {
        reply.ok = true;
        reply.payload =
            toJson(buildAdminInspectResponse(request, config, initializer, rpc, gateway));
    }
    catch (const std::exception& e)
    {
        reply.ok = false;
        reply.error = e.what();
    }
    return reply;
}
}  // namespace bcos::air::cli
