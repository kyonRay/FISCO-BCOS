#include "FallbackInspectors.h"
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
std::vector<std::string> readLogLines(
    const std::string& logPath, std::optional<int> tail, const std::string& levelFilter)
{
    namespace fs = std::filesystem;
    std::vector<std::string> lines;
    std::error_code ec;
    fs::path path(logPath);
    if (!fs::exists(path, ec))
    {
        return lines;
    }

    std::vector<fs::path> files;
    if (fs::is_regular_file(path, ec))
    {
        files.push_back(path);
    }
    else if (fs::is_directory(path, ec))
    {
        for (const auto& entry : fs::directory_iterator(path, ec))
        {
            if (entry.is_regular_file(ec))
            {
                files.push_back(entry.path());
            }
        }
    }

    std::sort(files.begin(), files.end());
    for (const auto& file : files)
    {
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line))
        {
            if (!levelFilter.empty() && line.find(levelFilter) == std::string::npos)
            {
                continue;
            }
            lines.push_back(line);
        }
    }

    if (tail && *tail > 0 && static_cast<size_t>(*tail) < lines.size())
    {
        lines.erase(lines.begin(), lines.end() - *tail);
    }
    return lines;
}
}  // namespace

namespace bcos::air::cli
{
InspectSection buildFallbackNodeSection(const InspectConfig& config)
{
    InspectSection section;
    section.available = true;
    section.data["preferredSource"] = config.preferredSource();
    section.data["chainID"] = config.chainID;
    section.data["rpcService"] = config.rpcServiceName;
    return section;
}

InspectSection buildFallbackChainSection(const InspectConfig& config, int timeoutMs)
{
    InspectSection section;
    section.available = false;
    section.reason = "chain runtime state is degraded in source=rpc+logs";
    section.data["chainID"] = config.chainID;
    section.data["timeoutMs"] = timeoutMs;
    section.data["rpcEndpoint"] = config.rpcListenIP + ":" + std::to_string(config.rpcListenPort);
    return section;
}

InspectSection buildFallbackNetworkSection(const InspectConfig& config)
{
    InspectSection section;
    section.available = true;
    section.data["rpcListenIP"] = config.rpcListenIP;
    section.data["rpcListenPort"] = config.rpcListenPort;
    section.data["web3Enabled"] = config.web3Enabled;
    section.data["web3ListenIP"] = config.web3ListenIP;
    section.data["web3ListenPort"] = config.web3ListenPort;
    section.data["gatewayService"] = config.gatewayServiceName;
    return section;
}

InspectSection buildFallbackStorageSection(const InspectConfig& config)
{
    InspectSection section;
    section.available = true;
    section.data["dataPath"] = config.storagePath;
    section.data["type"] = config.storageType;
    return section;
}

InspectSection buildFallbackLogSection(
    const InspectConfig& config, std::optional<int> tail, const std::string& levelFilter)
{
    InspectSection section;
    section.data["path"] = config.logPath;
    section.data["tail"] = tail ? *tail : 0;
    section.data["levelFilter"] = levelFilter;

    const auto lines = readLogLines(config.logPath, tail, levelFilter);
    if (lines.empty())
    {
        section.available = false;
        section.reason = "no readable log entries found in source=rpc+logs";
        return section;
    }

    section.available = true;
    Json::Value entries(Json::arrayValue);
    for (const auto& line : lines)
    {
        entries.append(line);
    }
    section.data["entries"] = std::move(entries);
    return section;
}

InspectSection buildUnavailableExecutorFallbackSection()
{
    InspectSection section;
    section.available = false;
    section.reason = "internal executor state unavailable in source=rpc+logs";
    section.data["source"] = "rpc+logs";
    return section;
}
}  // namespace bcos::air::cli
