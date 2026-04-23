#pragma once

#include "AdminIPCProtocol.h"
#include "InspectConfig.h"
#include "InspectTypes.h"
#include "libinitializer/Initializer.h"
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/rpc/RPCInterface.h>

namespace bcos::air::cli
{
InspectResponse buildAdminInspectResponse(const AdminInspectRequest& request,
    const InspectConfig& config, bcos::initializer::Initializer& initializer,
    const bcos::rpc::RPCInterface::Ptr& rpc, const bcos::gateway::GatewayInterface::Ptr& gateway);

AdminInspectReply buildAdminInspectReply(const AdminInspectRequest& request,
    const InspectConfig& config, bcos::initializer::Initializer& initializer,
    const bcos::rpc::RPCInterface::Ptr& rpc, const bcos::gateway::GatewayInterface::Ptr& gateway);
}  // namespace bcos::air::cli
