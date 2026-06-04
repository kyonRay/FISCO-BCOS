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
 * @file test_L2ConfigLoader.cpp
 * @brief L2ConfigLoaderImpl staticcall + ABI decode + hardfork loop (A6.7+A6.15).
 */
#include "../../src/executive/L2ConfigLoader.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <vector>

using namespace bcos;
using namespace bcos::executor;

namespace
{
// Append a 32-byte big-endian word holding `value` in its low 8 bytes.
void appendWord(bcos::bytes& out, uint64_t value)
{
    std::array<uint8_t, 32> word{};
    for (int i = 0; i < 8; ++i)
    {
        word[31 - i] = static_cast<uint8_t>(value >> (8 * i));
    }
    out.insert(out.end(), word.begin(), word.end());
}

// FakeStaticCaller dispatches on the 4-byte selector and, for hardfork queries,
// on the trailing uint8 id. Returns canned ABI bytes per (selector[, id]).
struct FakeStaticCaller
{
    // getChainConfig() return data (empty => simulate revert).
    bcos::bytes chainConfigReturn;
    // getHardforkActivation(id) -> return data, keyed by id. Missing id => empty.
    std::map<uint8_t, bcos::bytes> hardforkReturns;
    // Records each (addr, calldata) for assertions if needed.
    std::vector<std::pair<std::array<uint8_t, 20>, bcos::bytes>> calls;

    task::Task<bcos::bytes> call(const std::array<uint8_t, 20>& addr, bcos::bytesConstRef calldata)
    {
        calls.emplace_back(addr, bcos::bytes(calldata.begin(), calldata.end()));
        BOOST_REQUIRE(calldata.size() >= 4);
        std::array<uint8_t, 4> selector{calldata[0], calldata[1], calldata[2], calldata[3]};
        if (selector == SELECTOR_GET_CHAIN_CONFIG)
        {
            co_return chainConfigReturn;
        }
        if (selector == SELECTOR_GET_HARDFORK_ACTIVATION)
        {
            BOOST_REQUIRE_EQUAL(calldata.size(), 4U + 32U);
            uint8_t id = calldata[calldata.size() - 1];  // right-aligned uint8
            auto it = hardforkReturns.find(id);
            co_return it == hardforkReturns.end() ? bcos::bytes{} : it->second;
        }
        co_return bcos::bytes{};
    }
};

bcos::bytes makeChainConfig(uint64_t chainId, uint64_t gasLimit, uint64_t version, uint64_t flags)
{
    bcos::bytes ret;
    appendWord(ret, chainId);
    appendWord(ret, gasLimit);
    appendWord(ret, version);
    appendWord(ret, flags);
    return ret;
}

uint64_t chainIdLow64(const ledger::LedgerConfig& cfg)
{
    BOOST_REQUIRE(cfg.chainId().has_value());
    const auto& be = cfg.chainId().value();
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
    {
        value = (value << 8) | static_cast<uint64_t>(be.bytes[24 + i]);
    }
    return value;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(L2ConfigLoaderTest)

BOOST_AUTO_TEST_CASE(DecodesGetChainConfig)
{
    FakeStaticCaller caller;
    caller.chainConfigReturn = makeChainConfig(901, 30'000'000, 1, 0);
    // The getter always returns a full word; every id returns a zero timestamp so the
    // loader does not abort on a short return while no hardfork is activated.
    bcos::bytes zeroTs;
    appendWord(zeroTs, 0);
    for (uint8_t id = 0; id < L2_HARDFORK_COUNT; ++id)
    {
        caller.hardforkReturns[id] = zeroTs;
    }
    L2ConfigLoaderImpl<FakeStaticCaller> loader(&caller);

    ledger::LedgerConfig cfg;
    task::syncWait(loader.loadIntoLedgerConfig(100, cfg));

    BOOST_CHECK_EQUAL(chainIdLow64(cfg), 901U);
    BOOST_CHECK_EQUAL(std::get<0>(cfg.gasLimit()), 30'000'000U);
    BOOST_CHECK_EQUAL(std::get<1>(cfg.gasLimit()), 100);  // activation == loaded block
    BOOST_CHECK_EQUAL(cfg.compatibilityVersion(), 1U);
    // No hardfork timestamps configured -> none activated.
    for (uint8_t id = 0; id < L2_HARDFORK_COUNT; ++id)
    {
        auto flag = static_cast<ledger::Features::Flag>(
            static_cast<int>(ledger::Features::Flag::feature_l2_hardfork_bedrock) + id);
        BOOST_CHECK_EQUAL(cfg.features().activationBlockOf(flag), 0U);
    }
}

BOOST_AUTO_TEST_CASE(RevertAborts)
{
    FakeStaticCaller caller;
    caller.chainConfigReturn = {};  // empty == revert
    L2ConfigLoaderImpl<FakeStaticCaller> loader(&caller);

    ledger::LedgerConfig cfg;
    BOOST_CHECK_THROW(task::syncWait(loader.loadIntoLedgerConfig(1, cfg)), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(ShortReturnAborts)
{
    FakeStaticCaller caller;
    caller.chainConfigReturn = bcos::bytes(31, 0);  // 31 bytes < one word
    L2ConfigLoaderImpl<FakeStaticCaller> loader(&caller);

    ledger::LedgerConfig cfg;
    BOOST_CHECK_THROW(task::syncWait(loader.loadIntoLedgerConfig(1, cfg)), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(HardforkLoopRecordsOnlyEcotone)
{
    FakeStaticCaller caller;
    caller.chainConfigReturn = makeChainConfig(901, 30'000'000, 1, 0);
    // The contract's mapping getter always returns a full word; a missing id (no
    // canned return) is not allowed by the loader (short return aborts). Give every
    // id a full zero-ts word, then set ecotone (id 4) to a nonzero timestamp.
    bcos::bytes zeroTs;
    appendWord(zeroTs, 0);
    for (uint8_t id = 0; id < L2_HARDFORK_COUNT; ++id)
    {
        caller.hardforkReturns[id] = zeroTs;
    }
    bcos::bytes ecotoneTs;
    appendWord(ecotoneTs, 1700000000ULL);
    caller.hardforkReturns[4] = ecotoneTs;
    // id 2 keeps its explicit zero timestamp (configured but not activated).

    L2ConfigLoaderImpl<FakeStaticCaller> loader(&caller);
    ledger::LedgerConfig cfg;
    task::syncWait(loader.loadIntoLedgerConfig(100, cfg));

    const auto& features = cfg.features();
    BOOST_CHECK_EQUAL(
        features.activationBlockOf(ledger::Features::Flag::feature_l2_hardfork_ecotone),
        1700000000ULL);
    // Every other hardfork stays at default 0.
    for (uint8_t id = 0; id < L2_HARDFORK_COUNT; ++id)
    {
        if (id == 4)
        {
            continue;
        }
        auto flag = static_cast<ledger::Features::Flag>(
            static_cast<int>(ledger::Features::Flag::feature_l2_hardfork_bedrock) + id);
        BOOST_CHECK_EQUAL(features.activationBlockOf(flag), 0U);
    }
}

BOOST_AUTO_TEST_CASE(HardforkShortReturnAborts)
{
    FakeStaticCaller caller;
    caller.chainConfigReturn = makeChainConfig(901, 30'000'000, 1, 0);
    bcos::bytes zeroTs;
    appendWord(zeroTs, 0);
    for (uint8_t id = 0; id < L2_HARDFORK_COUNT; ++id)
    {
        caller.hardforkReturns[id] = zeroTs;
    }
    // One id returns 31 bytes (< one word): staticcall machinery failure -> abort.
    caller.hardforkReturns[5] = bcos::bytes(31, 0xff);

    L2ConfigLoaderImpl<FakeStaticCaller> loader(&caller);
    ledger::LedgerConfig cfg;
    BOOST_CHECK_THROW(task::syncWait(loader.loadIntoLedgerConfig(100, cfg)), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(OversizedGasLimitAborts)
{
    FakeStaticCaller caller;
    // gasLimit word 1 with a nonzero byte in the upper (above-uint64) region.
    caller.chainConfigReturn = makeChainConfig(901, 30'000'000, 1, 0);
    caller.chainConfigReturn[1 * 32] = 0x01;  // first byte of word 1 -> exceeds uint64
    L2ConfigLoaderImpl<FakeStaticCaller> loader(&caller);

    ledger::LedgerConfig cfg;
    BOOST_CHECK_THROW(task::syncWait(loader.loadIntoLedgerConfig(1, cfg)), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(ZeroChainIdAborts)
{
    FakeStaticCaller caller;
    caller.chainConfigReturn = makeChainConfig(0, 30'000'000, 1, 0);  // chainId == 0
    L2ConfigLoaderImpl<FakeStaticCaller> loader(&caller);

    ledger::LedgerConfig cfg;
    BOOST_CHECK_THROW(task::syncWait(loader.loadIntoLedgerConfig(1, cfg)), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
