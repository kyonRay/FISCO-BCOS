#include "InspectConfig.h"
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

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

    InspectConfig config;
    config.rpcListenIP = pt.get<std::string>("rpc.listen_ip", config.rpcListenIP);
    config.rpcListenPort = pt.get<int>("rpc.listen_port", config.rpcListenPort);
    config.web3Enabled = pt.get<bool>("web3_rpc.enable", config.web3Enabled);
    config.web3ListenIP = pt.get<std::string>("web3_rpc.listen_ip", config.web3ListenIP);
    config.web3ListenPort = pt.get<int>("web3_rpc.listen_port", config.web3ListenPort);
    config.chainID = pt.get<std::string>("chain.chain_id", config.chainID);
    config.rpcServiceName = pt.get<std::string>("service.rpc", config.rpcServiceName);
    config.gatewayServiceName = pt.get<std::string>("service.gateway", config.gatewayServiceName);
    config.storagePath = pt.get<std::string>("storage.data_path", config.storagePath);
    config.storageType = pt.get<std::string>("storage.type", config.storageType);
    config.logPath = pt.get<std::string>("log.log_path", config.logPath);
    config.adminEnabled =
        getConfigValue<bool>(pt, "admin.enable", "admin_ipc.enable", config.adminEnabled);
    config.adminIPCPath =
        getConfigValue<std::string>(pt, "admin.ipc_path", "admin_ipc.path", config.adminIPCPath);
    return config;
}

InspectConfig loadInspectConfig(const std::string& configPath)
{
    return InspectConfig::load(configPath);
}
}  // namespace bcos::air::cli
