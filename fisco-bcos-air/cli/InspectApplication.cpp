#include "InspectApplication.h"
#include "FallbackInspectors.h"
#include "InspectRenderer.h"
#include <ctime>
#include <iostream>

namespace
{
std::string nowAsString()
{
    std::time_t now = std::time(nullptr);
    std::tm currentTime{};
    localtime_r(&now, &currentTime);

    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &currentTime);
    return buffer;
}

bcos::air::cli::InspectSection makeUnavailableSection(const std::string& reason)
{
    bcos::air::cli::InspectSection section;
    section.available = false;
    section.reason = reason;
    return section;
}

void keepOnlyRequestedDomain(const std::string& domain, bcos::air::cli::InspectResponse& response)
{
    if (domain.empty())
    {
        return;
    }

    const auto hidden = makeUnavailableSection("");
    if (domain != "node")
    {
        response.node = hidden;
    }
    if (domain != "chain")
    {
        response.chain = hidden;
    }
    if (domain != "network")
    {
        response.network = hidden;
    }
    if (domain != "storage")
    {
        response.storage = hidden;
    }
    if (domain != "executor")
    {
        response.executor = hidden;
    }
    if (domain != "logs")
    {
        response.logs = hidden;
    }
}
}  // namespace

namespace bcos::air::cli
{
InspectApplication::InspectApplication(const bcos::initializer::Params& params)
  : m_params(params), m_config(InspectConfig::load(params.configFilePath))
{}

InspectApplication::InspectApplication(InspectConfig config) : m_config(std::move(config)) {}

std::string InspectApplication::selectSource() const
{
    if (m_config.adminEnabled && adminReachable())
    {
        return "admin-ipc";
    }
    return "rpc+logs";
}

InspectResponse InspectApplication::buildResponse(
    const bcos::initializer::CLIRequest& request, const InspectConfig& config)
{
    InspectResponse response;
    response.source = "rpc+logs";
    response.command = request.command;
    response.domain = request.domain;
    response.timestamp = nowAsString();
    response.summary.latestBlock = "degraded";
    response.summary.totalTx = "degraded";
    response.warnings.emplace_back(
        "In-process admin IPC is unavailable; using source=rpc+logs degraded view.");

    response.node = buildFallbackNodeSection(config);
    response.chain = buildFallbackChainSection(config, request.timeoutMs);
    response.network = buildFallbackNetworkSection(config);
    response.storage = buildFallbackStorageSection(config);
    response.executor = buildUnavailableExecutorFallbackSection();
    response.logs = buildFallbackLogSection(config, request.tail, request.logLevel);
    keepOnlyRequestedDomain(request.domain, response);
    return response;
}

bool InspectApplication::adminReachable() const
{
    if (m_adminReachabilityForTest)
    {
        return *m_adminReachabilityForTest;
    }
    return m_adminClient.reachable(m_config.adminIPCPath);
}

AdminInspectRequest InspectApplication::buildAdminRequest() const
{
    AdminInspectRequest request;
    request.command = m_params.cliRequest.command;
    request.domain = m_params.cliRequest.domain;
    request.jsonOutput = m_params.cliRequest.jsonOutput;
    request.verbose = m_params.cliRequest.verbose;
    request.showSource = m_params.cliRequest.showSource;
    request.timeoutMs = m_params.cliRequest.timeoutMs;
    request.tail = m_params.cliRequest.tail;
    request.logLevel = m_params.cliRequest.logLevel;
    return request;
}

InspectResponse InspectApplication::runFallback() const
{
    return buildResponse(m_params.cliRequest, m_config);
}

InspectResponse InspectApplication::runAdminIPC() const
{
    auto reply = m_adminClient.request(m_config.adminIPCPath, buildAdminRequest());
    if (!reply.ok)
    {
        auto response = runFallback();
        response.source = "rpc+logs";
        response.warnings.emplace_back("admin-ipc request failed: " + reply.error);
        return response;
    }

    InspectResponse response;
    response.source = reply.payload.get("source", "admin-ipc").asString();
    response.command = reply.payload.get("command", m_params.cliRequest.command).asString();
    response.domain = reply.payload.get("domain", m_params.cliRequest.domain).asString();
    response.timestamp = reply.payload.get("timestamp", nowAsString()).asString();
    response.summary.latestBlock = reply.payload["summary"].get("latestBlock", "").asString();
    response.summary.totalTx = reply.payload["summary"].get("totalTx", "").asString();

    auto loadSection = [](const Json::Value& payload, const char* name) {
        InspectSection section;
        if (payload.isMember(name))
        {
            auto const& json = payload[name];
            section.available = json.get("available", false).asBool();
            section.reason = json.get("reason", "").asString();
            section.data = json.get("data", Json::objectValue);
        }
        return section;
    };

    response.node = loadSection(reply.payload, "node");
    response.chain = loadSection(reply.payload, "chain");
    response.network = loadSection(reply.payload, "network");
    response.storage = loadSection(reply.payload, "storage");
    response.executor = loadSection(reply.payload, "executor");
    response.logs = loadSection(reply.payload, "logs");
    if (reply.payload.isMember("warnings") && reply.payload["warnings"].isArray())
    {
        for (const auto& warning : reply.payload["warnings"])
        {
            response.warnings.emplace_back(warning.asString());
        }
    }
    keepOnlyRequestedDomain(response.domain, response);
    return response;
}

int InspectApplication::run() const
{
    auto response = selectSource() == "admin-ipc" ? runAdminIPC() : runFallback();
    std::cout << renderInspectResponse(response, m_params.cliRequest.jsonOutput) << std::endl;
    return 0;
}

int runAirInspectCLI(const bcos::initializer::Params& params)
{
    return InspectApplication(params).run();
}
}  // namespace bcos::air::cli
