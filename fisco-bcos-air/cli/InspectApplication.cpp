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
std::string InspectApplication::selectSource(const InspectConfig& config)
{
    return config.preferredSource();
}

InspectResponse InspectApplication::buildResponse(
    const bcos::initializer::CLIRequest& request, const InspectConfig& config)
{
    InspectResponse response;
    response.source = selectSource(config);
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

int InspectApplication::run() const
{
    auto config = InspectConfig::load(m_params.configFilePath);
    auto response = buildResponse(m_params.cliRequest, config);
    std::cout << renderInspectResponse(response, m_params.cliRequest.jsonOutput) << std::endl;
    return 0;
}

int runAirInspectCLI(const bcos::initializer::Params& params)
{
    return InspectApplication(params).run();
}
}  // namespace bcos::air::cli
