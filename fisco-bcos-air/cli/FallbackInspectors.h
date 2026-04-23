#pragma once

#include "InspectConfig.h"
#include "InspectTypes.h"
#include <optional>
#include <string>

namespace bcos::air::cli
{
InspectSection buildFallbackNodeSection(const InspectConfig& config);
InspectSection buildFallbackChainSection(const InspectConfig& config, int timeoutMs);
InspectSection buildFallbackNetworkSection(const InspectConfig& config);
InspectSection buildFallbackStorageSection(const InspectConfig& config);
InspectSection buildFallbackLogSection(
    const InspectConfig& config, std::optional<int> tail, const std::string& levelFilter);
InspectSection buildUnavailableExecutorFallbackSection();
}  // namespace bcos::air::cli
