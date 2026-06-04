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
 * @file L2ConfigLoader.h
 * @brief Per-block loader that reads the L2 SystemConfig predeploy via EVM
 *        staticcall and projects it onto LedgerConfig + Features. Solidity is the
 *        single authoritative source for the hardfork schedule (spec §4.2); a
 *        failed staticcall throws to abort the current block, never falls back.
 */
#pragma once
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/IL2ConfigLoader.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace bcos::executor
{
// Predeploy address of SystemConfig.sol: 0x42000000000000000000000000000000000000C0.
inline constexpr std::array<uint8_t, 20> L2_SYSTEM_CONFIG_ADDRESS = {0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0};

// Function selectors (first 4 bytes of keccak256(signature)), measured from the
// PR-1 contract with `cast sig`:
//   getChainConfig()             -> 0x606c0c94
//   getHardforkActivation(uint8) -> 0x98081dab
inline constexpr std::array<uint8_t, 4> SELECTOR_GET_CHAIN_CONFIG = {0x60, 0x6c, 0x0c, 0x94};
inline constexpr std::array<uint8_t, 4> SELECTOR_GET_HARDFORK_ACTIVATION = {0x98, 0x08, 0x1d, 0xab};

inline constexpr size_t L2_ABI_WORD = 32;
// Number of OP-Stack L2 hardforks (BEDROCK..ISTHMUS), derived from the L2Hardfork
// enum so the loop bound and the enum cannot drift apart.
inline constexpr size_t L2_HARDFORK_COUNT =
    static_cast<size_t>(bcos::ledger::L2Hardfork::ISTHMUS) + 1;

// The hardfork loop maps `Flag(feature_l2_hardfork_bedrock) + id` onto the L2Hardfork
// enum value `id`. These two parallel mappings have no runtime link, so guard the
// invariant at compile time: adding/reordering one side without the other fails to
// build instead of silently activating the wrong hardfork.
static_assert(L2_HARDFORK_COUNT == 9, "L2Hardfork enum range changed; revisit the loader");
static_assert(static_cast<int>(bcos::ledger::Features::Flag::feature_l2_hardfork_isthmus) -
                      static_cast<int>(bcos::ledger::Features::Flag::feature_l2_hardfork_bedrock) ==
                  static_cast<int>(bcos::ledger::L2Hardfork::ISTHMUS) -
                      static_cast<int>(bcos::ledger::L2Hardfork::BEDROCK),
    "Features::Flag::feature_l2_hardfork_* range and L2Hardfork enum range diverged");

// Big-endian decode of the low 8 bytes of a 32-byte ABI word at `data + offset`.
inline uint64_t l2DecodeUint64(const bcos::bytes& data, size_t wordOffset)
{
    uint64_t value = 0;
    for (size_t i = wordOffset + L2_ABI_WORD - 8; i < wordOffset + L2_ABI_WORD; ++i)
    {
        value = (value << 8) | static_cast<uint64_t>(data[i]);
    }
    return value;
}

/// StaticCaller concept: must expose
///   task::Task<bcos::bytes> call(const std::array<uint8_t, 20>& addr, bcos::bytesConstRef
///   calldata)
/// performing an EVM staticcall and returning the raw return data (empty on revert).
/// The caller pointer is borrowed, not owned: the StaticCaller must outlive this
/// L2ConfigLoaderImpl.
template <typename StaticCaller>
class L2ConfigLoaderImpl : public ledger::IL2ConfigLoader
{
public:
    explicit L2ConfigLoaderImpl(StaticCaller* caller) : m_caller(caller)
    {
        assert(m_caller != nullptr);
    }

    /// Precondition: `out` must be freshly constructed. This projects the predeploy
    /// state onto `out` additively; it does NOT clear stale hardfork activations, so
    /// reusing an `out` from a previous block would leak old activations.
    task::Task<void> loadIntoLedgerConfig(
        protocol::BlockNumber blockNumber, ledger::LedgerConfig& out) override
    {
        // === A6.7: getChainConfig() -> (chainId, gasLimit, version, featureFlags) ===
        bcos::bytes chainCalldata(
            SELECTOR_GET_CHAIN_CONFIG.begin(), SELECTOR_GET_CHAIN_CONFIG.end());
        auto chainRet = co_await m_caller->call(L2_SYSTEM_CONFIG_ADDRESS,
            bcos::bytesConstRef(chainCalldata.data(), chainCalldata.size()));

        // Four 32-byte words required. Short return (revert / OOG / truncation) must
        // abort the block, never silently fall back to a cached config.
        if (chainRet.size() < 4 * L2_ABI_WORD)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "L2ConfigLoader: getChainConfig() returned " + std::to_string(chainRet.size()) +
                " bytes, expected at least " + std::to_string(4 * L2_ABI_WORD)));
        }

        // word 0: chainId (uint256) -> evmc_uint256be (32-byte big-endian, copied verbatim).
        evmc_uint256be chainId{};
        std::copy_n(chainRet.data(), L2_ABI_WORD, chainId.bytes);
        // chainId == 0 means the genesis immutable was never patched. SystemConfig.sol's
        // GENESIS CONSTRAINT requires a nonzero chainId; a zero here breaks EIP-155 replay
        // protection, so abort rather than run a chain with an unusable chainId.
        bool chainIdNonZero = false;
        for (uint8_t byte : chainId.bytes)
        {
            if (byte != 0)
            {
                chainIdNonZero = true;
                break;
            }
        }
        if (!chainIdNonZero)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "L2ConfigLoader: getChainConfig() returned chainId == 0 (genesis immutable not "
                "patched; breaks EIP-155)"));
        }
        out.setChainId(chainId);

        // word 1: l2BlockGasLimit (uint64). LedgerConfig stores gasLimit as
        // tuple<uint64_t, BlockNumber>; the activation number is the block we
        // loaded it for. The contract types this as uint64, so the upper 24 bytes
        // must be zero; anything else is an ABI/contract mismatch on a consensus
        // parameter -> abort.
        for (size_t i = 1 * L2_ABI_WORD; i < 2 * L2_ABI_WORD - 8; ++i)
        {
            if (chainRet[i] != 0)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error(
                    "L2ConfigLoader: getChainConfig() gasLimit word exceeds uint64 (nonzero upper "
                    "byte at offset " +
                    std::to_string(i) + ")"));
            }
        }
        uint64_t gasLimit = l2DecodeUint64(chainRet, 1 * L2_ABI_WORD);
        out.setGasLimit({gasLimit, blockNumber});

        // word 2: compatibilityVersion (uint32) -> low 4 bytes of the word. The
        // contract types this as uint32, so the upper 28 bytes must be zero; anything
        // else is an ABI/contract mismatch on a consensus parameter -> abort.
        for (size_t i = 2 * L2_ABI_WORD; i < 3 * L2_ABI_WORD - 4; ++i)
        {
            if (chainRet[i] != 0)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error(
                    "L2ConfigLoader: getChainConfig() version word exceeds uint32 (nonzero upper "
                    "byte at offset " +
                    std::to_string(i) + ")"));
            }
        }
        uint64_t version = l2DecodeUint64(chainRet, 2 * L2_ABI_WORD);
        out.setCompatibilityVersion(static_cast<uint32_t>(version));

        // word 3: featureFlags (uint256). Phase A placeholder — the bit layout is
        // not yet defined, so it is intentionally ignored here.

        // === A6.15: getHardforkActivation(uint8 id) for ids 0..8 ===
        // Carrier note: LedgerConfig already owns a Features by value
        // (LedgerConfig::features()/setFeatures()). We mutate a local copy and set
        // it back so all downstream consumers read activations off LedgerConfig.
        auto features = out.features();
        for (uint8_t id = 0; id < L2_HARDFORK_COUNT; ++id)
        {
            bcos::bytes hfCalldata(
                SELECTOR_GET_HARDFORK_ACTIVATION.begin(), SELECTOR_GET_HARDFORK_ACTIVATION.end());
            // 32-byte right-aligned uint8 argument: 31 zero bytes then the id.
            hfCalldata.resize(SELECTOR_GET_HARDFORK_ACTIVATION.size() + L2_ABI_WORD, 0);
            hfCalldata.back() = id;

            auto hfRet = co_await m_caller->call(L2_SYSTEM_CONFIG_ADDRESS,
                bcos::bytesConstRef(hfCalldata.data(), hfCalldata.size()));

            // The mapping getter always returns a full 32-byte word. A short return
            // means the staticcall machinery failed (revert / OOG / truncation), the
            // same failure mode getChainConfig() aborts on -> abort the block, never
            // silently skip a hardfork.
            if (hfRet.size() < L2_ABI_WORD)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error(
                    "L2ConfigLoader: getHardforkActivation(" + std::to_string(id) + ") returned " +
                    std::to_string(hfRet.size()) + " bytes, expected at least " +
                    std::to_string(L2_ABI_WORD)));
            }
            uint64_t timestamp = l2DecodeUint64(hfRet, 0);
            if (timestamp == 0)
            {
                // 0 == not activated; leave the sparse map without an entry. This is
                // the only legitimate skip: the word was returned in full.
                continue;
            }
            auto flag = static_cast<ledger::Features::Flag>(
                static_cast<int>(ledger::Features::Flag::feature_l2_hardfork_bedrock) + id);
            features.setActivationBlockOf(flag, timestamp);
        }
        out.setFeatures(std::move(features));
        co_return;
    }

private:
    StaticCaller* m_caller;
};
}  // namespace bcos::executor
