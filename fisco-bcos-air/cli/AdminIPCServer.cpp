#include "AdminIPCServer.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>

namespace
{
constexpr int c_invalidFD = -1;

std::string readRequestLine(int fd)
{
    std::string payload;
    char ch = '\0';
    while (true)
    {
        auto bytesRead = ::read(fd, &ch, 1);
        if (bytesRead <= 0)
        {
            break;
        }
        if (ch == '\n')
        {
            break;
        }
        payload.push_back(ch);
    }
    return payload;
}

void writeAll(int fd, const std::string& payload)
{
    size_t offset = 0;
    while (offset < payload.size())
    {
        auto written = ::write(fd, payload.data() + offset, payload.size() - offset);
        if (written <= 0)
        {
            return;
        }
        offset += static_cast<size_t>(written);
    }
}
}  // namespace

namespace bcos::air::cli
{
void AdminIPCServer::start(const InspectConfig& config, Handler handler)
{
    stop();
    if (!config.adminEnabled || config.adminIPCPath.empty())
    {
        return;
    }

    namespace fs = std::filesystem;
    fs::create_directories(fs::path(config.adminIPCPath).parent_path());
    ::unlink(config.adminIPCPath.c_str());

    m_serverFD = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_serverFD == c_invalidFD)
    {
        throw std::runtime_error("failed to create admin ipc socket");
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (config.adminIPCPath.size() >= sizeof(address.sun_path))
    {
        ::close(m_serverFD);
        m_serverFD = c_invalidFD;
        throw std::runtime_error("admin ipc path is too long");
    }
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", config.adminIPCPath.c_str());
    if (::bind(m_serverFD, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(m_serverFD);
        m_serverFD = c_invalidFD;
        throw std::runtime_error("failed to bind admin ipc socket");
    }
    ::chmod(config.adminIPCPath.c_str(), 0600);
    if (::listen(m_serverFD, 16) != 0)
    {
        ::close(m_serverFD);
        m_serverFD = c_invalidFD;
        throw std::runtime_error("failed to listen on admin ipc socket");
    }

    m_socketPath = config.adminIPCPath;
    m_handler = std::move(handler);
    m_running = true;
    m_thread.emplace([this]() { runAcceptLoop(); });
}

void AdminIPCServer::stop()
{
    if (!m_running && m_serverFD == c_invalidFD)
    {
        return;
    }

    m_running = false;
    if (m_serverFD != c_invalidFD)
    {
        ::shutdown(m_serverFD, SHUT_RDWR);
        ::close(m_serverFD);
        m_serverFD = c_invalidFD;
    }
    if (m_thread && m_thread->joinable())
    {
        m_thread->join();
    }
    m_thread.reset();

    if (!m_socketPath.empty())
    {
        ::unlink(m_socketPath.c_str());
    }
    m_socketPath.clear();
}

void AdminIPCServer::runAcceptLoop()
{
    while (m_running)
    {
        auto clientFD = ::accept(m_serverFD, nullptr, nullptr);
        if (clientFD == c_invalidFD)
        {
            if (m_running)
            {
                continue;
            }
            break;
        }
        handleConnection(clientFD);
        ::close(clientFD);
    }
}

void AdminIPCServer::handleConnection(int nativeSocket)
{
    AdminInspectReply reply;
    try
    {
        auto payload = readRequestLine(nativeSocket);
        auto request = deserializeAdminInspectRequest(payload);
        if (!m_handler)
        {
            reply.error = "admin ipc handler is not configured";
        }
        else
        {
            reply = m_handler(request);
        }
    }
    catch (const std::exception& e)
    {
        reply.ok = false;
        reply.error = e.what();
    }

    writeAll(nativeSocket, serializeAdminInspectReply(reply));
}
}  // namespace bcos::air::cli
