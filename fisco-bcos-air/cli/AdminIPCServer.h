#pragma once

#include "AdminIPCProtocol.h"
#include "InspectConfig.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace bcos::air::cli
{
class AdminIPCServer
{
public:
    using Handler = std::function<AdminInspectReply(const AdminInspectRequest&)>;

    AdminIPCServer() = default;
    AdminIPCServer(const AdminIPCServer&) = delete;
    AdminIPCServer(AdminIPCServer&&) = delete;
    AdminIPCServer& operator=(const AdminIPCServer&) = delete;
    AdminIPCServer& operator=(AdminIPCServer&&) = delete;
    ~AdminIPCServer() { stop(); }

    void start(const InspectConfig& config, Handler handler);
    void stop();

private:
    void runAcceptLoop();
    void handleConnection(int nativeSocket);

    std::atomic_bool m_running = false;
    std::optional<std::thread> m_thread;
    std::string m_socketPath;
    Handler m_handler;
    int m_serverFD = -1;
};
}  // namespace bcos::air::cli
