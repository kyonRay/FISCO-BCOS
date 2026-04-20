#include "AdminIPCClient.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <sstream>

namespace
{
using Socket = boost::asio::local::stream_protocol::socket;
using Endpoint = boost::asio::local::stream_protocol::endpoint;

std::string readAll(Socket& socket)
{
    std::string payload;
    boost::system::error_code ec;
    for (;;)
    {
        char buffer[1024] = {0};
        auto bytesRead = socket.read_some(boost::asio::buffer(buffer), ec);
        if (bytesRead > 0)
        {
            payload.append(buffer, bytesRead);
        }
        if (ec == boost::asio::error::eof)
        {
            break;
        }
        if (ec)
        {
            throw boost::system::system_error(ec);
        }
    }
    return payload;
}
}  // namespace

namespace bcos::air::cli
{
bool AdminIPCClient::reachable(const std::string& socketPath) const
{
    try
    {
        boost::asio::io_context io;
        Socket socket(io);
        socket.connect(Endpoint(socketPath));
        socket.close();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

AdminInspectReply AdminIPCClient::request(
    const std::string& socketPath, const AdminInspectRequest& request) const
{
    try
    {
        boost::asio::io_context io;
        Socket socket(io);
        socket.connect(Endpoint(socketPath));

        auto payload = serializeAdminInspectRequest(request);
        payload.push_back('\n');
        boost::asio::write(socket, boost::asio::buffer(payload));
        socket.shutdown(boost::asio::socket_base::shutdown_send);

        return deserializeAdminInspectReply(readAll(socket));
    }
    catch (const std::exception& e)
    {
        AdminInspectReply reply;
        reply.error = e.what();
        return reply;
    }
}
}  // namespace bcos::air::cli
