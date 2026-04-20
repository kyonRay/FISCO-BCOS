#pragma once

#include "AdminIPCClient.h"
#include "AdminIPCProtocol.h"
#include "InspectConfig.h"
#include "InspectTypes.h"
#include "libinitializer/CommandHelper.h"
#include <optional>

namespace bcos::air::cli
{
class InspectApplication
{
public:
    explicit InspectApplication(const bcos::initializer::Params& params);
    explicit InspectApplication(InspectConfig config);

    void setAdminReachabilityForTest(bool reachable) { m_adminReachabilityForTest = reachable; }

    std::string selectSource() const;
    int run() const;

    static InspectResponse buildResponse(
        const bcos::initializer::CLIRequest& request, const InspectConfig& config);

private:
    bool adminReachable() const;
    InspectResponse runFallback() const;
    InspectResponse runAdminIPC() const;
    AdminInspectRequest buildAdminRequest() const;

    bcos::initializer::Params m_params;
    InspectConfig m_config;
    AdminIPCClient m_adminClient;
    std::optional<bool> m_adminReachabilityForTest;
};

int runAirInspectCLI(const bcos::initializer::Params& params);
}  // namespace bcos::air::cli
