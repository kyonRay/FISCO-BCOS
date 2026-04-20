#pragma once

#include "AdminIPCProtocol.h"
#include <string>

namespace bcos::air::cli
{
class AdminIPCClient
{
public:
    bool reachable(const std::string& socketPath) const;
    AdminInspectReply request(
        const std::string& socketPath, const AdminInspectRequest& request) const;
};
}  // namespace bcos::air::cli
