/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Unit tests for FIB-84: feature-aware precompiled lookup
 */

#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::executor_v1;
using namespace bcos::ledger;

struct PrecompiledFeatureGateFixture
{
    bcos::crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    PrecompiledManager manager{hashImpl};
    Features features;
};

BOOST_FIXTURE_TEST_SUITE(FIB84_PrecompiledFeatureGateTest, PrecompiledFeatureGateFixture)

// --- Non-gated precompileds should always be returned ---

BOOST_AUTO_TEST_CASE(NonGatedPrecompiled_AlwaysReturned)
{
    // ecrecover (0x1) has no feature flag
    auto* result = manager.getPrecompiled(1UL, features);
    BOOST_CHECK(result != nullptr);

    // SystemConfig (0x1000) has no feature flag
    result = manager.getPrecompiled(0x1000UL, features);
    BOOST_CHECK(result != nullptr);

    // CryptoPrecompiled (0x100a) has no feature flag
    result = manager.getPrecompiled(0x100aUL, features);
    BOOST_CHECK(result != nullptr);
}

// --- ShardingPrecompiled (0x1010) gated by feature_sharding ---

BOOST_AUTO_TEST_CASE(ShardingPrecompiled_BlockedWhenDisabled)
{
    // feature_sharding is disabled by default
    auto* result = manager.getPrecompiled(0x1010UL, features);
    BOOST_CHECK(result == nullptr);
}

BOOST_AUTO_TEST_CASE(ShardingPrecompiled_ReturnedWhenEnabled)
{
    features.set(Features::Flag::feature_sharding);
    auto* result = manager.getPrecompiled(0x1010UL, features);
    BOOST_CHECK(result != nullptr);
}

// --- PaillierPrecompiled (0x5003) gated by feature_paillier (FIB-84 fix) ---

BOOST_AUTO_TEST_CASE(PaillierPrecompiled_BlockedWhenDisabled)
{
    // feature_paillier is disabled by default
    auto* result = manager.getPrecompiled(0x5003UL, features);
    BOOST_CHECK(result == nullptr);
}

BOOST_AUTO_TEST_CASE(PaillierPrecompiled_ReturnedWhenEnabled)
{
    features.set(Features::Flag::feature_paillier);
    auto* result = manager.getPrecompiled(0x5003UL, features);
    BOOST_CHECK(result != nullptr);
}

// --- BalancePrecompiled (0x1011) gated by feature_balance_precompiled ---

BOOST_AUTO_TEST_CASE(BalancePrecompiled_BlockedWhenDisabled)
{
    auto* result = manager.getPrecompiled(0x1011UL, features);
    BOOST_CHECK(result == nullptr);
}

BOOST_AUTO_TEST_CASE(BalancePrecompiled_ReturnedWhenEnabled)
{
    features.set(Features::Flag::feature_balance_precompiled);
    auto* result = manager.getPrecompiled(0x1011UL, features);
    BOOST_CHECK(result != nullptr);
}

// --- evmc_address overload ---

BOOST_AUTO_TEST_CASE(EvmcAddressOverload_FeatureGateEnforced)
{
    // Build evmc_address for 0x5003 (PaillierPrecompiled)
    evmc_address addr{};
    addr.bytes[18] = 0x50;
    addr.bytes[19] = 0x03;

    // Disabled → nullptr
    auto* result = manager.getPrecompiled(addr, features);
    BOOST_CHECK(result == nullptr);

    // Enabled → non-null
    features.set(Features::Flag::feature_paillier);
    result = manager.getPrecompiled(addr, features);
    BOOST_CHECK(result != nullptr);
}

// --- Non-feature-aware overload still works (backward compat for internal use) ---

BOOST_AUTO_TEST_CASE(NonFeatureAwareOverload_AlwaysReturns)
{
    // The old overload ignores feature flags — returns the precompiled regardless
    auto* result = manager.getPrecompiled(0x5003UL);
    BOOST_CHECK(result != nullptr);

    result = manager.getPrecompiled(0x1010UL);
    BOOST_CHECK(result != nullptr);
}

// --- Unknown address returns nullptr ---

BOOST_AUTO_TEST_CASE(UnknownAddress_ReturnsNullptr)
{
    auto* result = manager.getPrecompiled(0xFFFFUL, features);
    BOOST_CHECK(result == nullptr);

    result = manager.getPrecompiled(0xFFFFUL);
    BOOST_CHECK(result == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
