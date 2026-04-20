#include "InspectConfig.h"
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>

namespace
{
template <class T>
T getConfigValue(const boost::property_tree::ptree& pt, const std::string& primaryKey,
    const std::string& fallbackKey, const T& defaultValue)
{
    if (const auto value = pt.get_optional<T>(primaryKey); value)
    {
        return *value;
    }
    if (!fallbackKey.empty())
    {
        if (const auto value = pt.get_optional<T>(fallbackKey); value)
        {
            return *value;
        }
    }
    return defaultValue;
}

std::string resolvePath(const std::filesystem::path& baseDir, const std::string& rawPath)
{
    if (rawPath.empty())
    {
        return rawPath;
    }

    std::filesystem::path path(rawPath);
    if (path.is_absolute())
    {
        return path.lexically_normal().string();
    }

    if (baseDir.empty())
    {
        return path.lexically_normal().string();
    }
    return (baseDir / path).lexically_normal().string();
}
}  // namespace

namespace bcos::air::cli
{
std::string InspectConfig::preferredSource() const
{
    return adminEnabled ? "admin-ipc" : "rpc+logs";
}

InspectConfig InspectConfig::load(const std::string& configPath)
{
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(configPath, pt);
    auto baseDir = std::filesystem::path(configPath).parent_path();

    InspectConfig config;
    config.rpcListenIP = pt.get<std::string>("rpc.listen_ip", config.rpcListenIP);
    config.rpcListenPort = pt.get<int>("rpc.listen_port", config.rpcListenPort);
    config.web3Enabled = pt.get<bool>("web3_rpc.enable", config.web3Enabled);
    config.web3ListenIP = pt.get<std::string>("web3_rpc.listen_ip", config.web3ListenIP);
    config.web3ListenPort = pt.get<int>("web3_rpc.listen_port", config.web3ListenPort);
    config.chainID = pt.get<std::string>("chain.chain_id", config.chainID);
    config.rpcServiceName = pt.get<std::string>("service.rpc", config.rpcServiceName);
    config.gatewayServiceName = pt.get<std::string>("service.gateway", config.gatewayServiceName);
    config.storagePath =
        resolvePath(baseDir, pt.get<std::string>("storage.data_path", config.storagePath));
    config.storageType = pt.get<std::string>("storage.type", config.storageType);
    config.logPath = resolvePath(baseDir, pt.get<std::string>("log.log_path", config.logPath));
    config.adminEnabled =
        getConfigValue<bool>(pt, "admin.enable", "admin_ipc.enable", config.adminEnabled);
    config.adminIPCPath = resolvePath(baseDir,
        getConfigValue<std::string>(pt, "admin.ipc_path", "admin_ipc.path", config.adminIPCPath));
    return config;
}

InspectConfig loadInspectConfig(const std::string& configPath)
{
    return InspectConfig::load(configPath);
}
}  // namespace bcos::air::cli
