#pragma once

#include <string>

namespace bcos::air::cli
{
struct InspectConfig
{
    bool adminEnabled = false;
    std::string adminIPCPath = "./run/admin.sock";
    std::string rpcListenIP = "127.0.0.1";
    int rpcListenPort = 20200;
    bool web3Enabled = false;
    std::string web3ListenIP = "127.0.0.1";
    int web3ListenPort = 8545;
    std::string chainID = "chain0";
    std::string rpcServiceName;
    std::string gatewayServiceName;
    std::string storagePath = "data";
    std::string storageType = "RocksDB";
    std::string logPath = "./log";

    std::string preferredSource() const;
    static InspectConfig load(const std::string& configPath);
};

InspectConfig loadInspectConfig(const std::string& configPath);
}  // namespace bcos::air::cli
