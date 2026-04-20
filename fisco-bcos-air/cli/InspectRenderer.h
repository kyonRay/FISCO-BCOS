#pragma once

#include "InspectTypes.h"
#include <string>

namespace bcos::air::cli
{
std::string renderHumanReadable(const InspectResponse& response);
std::string renderJson(const InspectResponse& response);
std::string renderInspectResponse(const InspectResponse& response, bool jsonOutput);
}  // namespace bcos::air::cli
