/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file SystemConfigs.h
 * @author: kyonGuo
 * @date 2024/5/22
 */

#pragma once
#include "../protocol/ProtocolTypeDef.h"
#include <boost/throw_exception.hpp>
#include <magic_enum/magic_enum.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/transform.hpp>

namespace bcos::ledger
{
DERIVE_BCOS_EXCEPTION(NoSuchSystemConfig);

/// IMPORTANT!!
/// DO NOT change the name of enum. It is used to get the name of the config.
enum class SystemConfig
{
    tx_gas_limit = 0,
    tx_gas_price,
    tx_count_limit,
    consensus_leader_period,
    auth_check_status,
    compatibility_version,
    feature_rpbft,
    feature_rpbft_epoch_block_num,
    feature_rpbft_epoch_sealer_num,
    feature_rpbft_notify_rotate,
    feature_balance_precompiled,
    web3_chain_id,
    balance_transfer,
    executor_version,
};
// L2 mode config keys. The hardfork-activation key is a prefix: callers append
// the uint8 hardfork id (matching L2Hardfork below) to form the full key.
constexpr static std::string_view SYSTEM_KEY_L2_HARDFORK_ACTIVATION =
    "l2_hardfork_activation_";  // concatenate the hardfork id as suffix
constexpr static std::string_view SYSTEM_KEY_L2_FEATURE_FLAGS = "l2_feature_flags";

// OP-Stack L2 hardfork ids. These map 1:1 onto the uint8 argument of
// SystemConfig.getHardforkActivation(uint8) and onto the contiguous
// Features::Flag::feature_l2_hardfork_* enum range.
enum class L2Hardfork : uint8_t
{
    BEDROCK = 0,
    REGOLITH = 1,
    CANYON = 2,
    DELTA = 3,
    ECOTONE = 4,
    FJORD = 5,
    GRANITE = 6,
    HOLOCENE = 7,
    ISTHMUS = 8,
};

struct SystemConfigs
{
public:
    using ConfigPair = std::pair<std::string, protocol::BlockNumber>;

    static SystemConfig fromString(std::string_view str)
    {
        auto const value = magic_enum::enum_cast<SystemConfig>(str);
        if (!value)
        {
            BOOST_THROW_EXCEPTION(NoSuchSystemConfig{});
        }
        return *value;
    }

    std::optional<ConfigPair> get(SystemConfig config) const
    {
        if (magic_enum::enum_contains(config))
        {
            return m_sysConfigs.at(static_cast<int>(config));
        }
        return {};
    }

    ConfigPair getOrDefault(SystemConfig config, std::string defaultValue) const
    {
        return get(config).value_or(ConfigPair{std::move(defaultValue), 0});
    }

    std::optional<ConfigPair> get(std::string_view config) const { return get(fromString(config)); }

    void set(SystemConfig config, std::string value, protocol::BlockNumber number)
    {
        m_sysConfigs.at(static_cast<int>(config)) = {std::move(value), number};
    }
    void set(std::string_view config, std::string value, protocol::BlockNumber number)
    {
        set(fromString(config), std::move(value), number);
    }

    auto systemConfigs() const
    {
        return ::ranges::views::iota(std::size_t{0}, std::size_t{m_sysConfigs.size()}) |
               ::ranges::views::transform([this](size_t index) {
                   auto flag = magic_enum::enum_value<SystemConfig>(index);
                   return std::make_tuple(
                       flag, magic_enum::enum_name(flag), m_sysConfigs.at(index));
               });
    }

    // return all config names
    static auto supportConfigs()
    {
        return ::ranges::views::iota(
                   std::size_t{0}, std::size_t{magic_enum::enum_count<SystemConfig>()}) |
               ::ranges::views::transform([](size_t index) {
                   auto flag = magic_enum::enum_value<SystemConfig>(index);
                   return magic_enum::enum_name(flag);
               });
    }

private:
    std::array<std::optional<ConfigPair>, magic_enum::enum_count<SystemConfig>()> m_sysConfigs;
};

}  // namespace bcos::ledger
