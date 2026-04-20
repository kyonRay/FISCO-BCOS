#pragma once

#include "InspectConfig.h"
#include "InspectTypes.h"
#include "libinitializer/CommandHelper.h"

namespace bcos::air::cli
{
class InspectApplication
{
public:
    explicit InspectApplication(const bcos::initializer::Params& params) : m_params(params) {}

    int run() const;

    static std::string selectSource(const InspectConfig& config);
    static InspectResponse buildResponse(
        const bcos::initializer::CLIRequest& request, const InspectConfig& config);

private:
    bcos::initializer::Params m_params;
};

int runAirInspectCLI(const bcos::initializer::Params& params);
}  // namespace bcos::air::cli
