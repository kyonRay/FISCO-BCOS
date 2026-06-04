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
 * @file test_GenesisImmutableHash.cpp
 */
#include "bcos-ledger/GenesisImmutableHash.h"
#include <bcos-framework/ledger/GenesisConfig.h>
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <string>

using namespace bcos;
using namespace bcos::ledger;

namespace bcos::test
{
namespace
{
// baseConfig: l2 chain with one funded contract alloc.
GenesisConfig baseConfig()
{
    GenesisConfig genesis;
    genesis.m_chainMode = "l2";
    genesis.m_chainID = "901";
    genesis.m_groupID = "group0";
    genesis.m_smCrypto = false;
    genesis.m_isWasm = false;
    genesis.m_allocs.push_back(Alloc{.address = "0x" + std::string(40, '0'),
        .balance = u256(1000),
        .nonce = "1",
        .code = "0xaa",
        .storage = {}});
    return genesis;
}

// 64-hex-char (32-byte) storage key built from a single repeated byte.
std::string key32(char nibble)
{
    return "0x" + std::string(64, nibble);
}

// value of `bytes` 32-byte words, each filled with the repeated `nibble`.
std::string valueBytes(char nibble, int words)
{
    return "0x" + std::string(static_cast<size_t>(words) * 64, nibble);
}

// L2 config with a single alloc whose storage is given.
GenesisConfig configWithStorage(std::vector<Alloc::State> storage)
{
    GenesisConfig genesis;
    genesis.m_chainMode = "l2";
    genesis.m_chainID = "901";
    genesis.m_groupID = "group0";
    genesis.m_smCrypto = false;
    genesis.m_isWasm = false;
    genesis.m_allocs.push_back(Alloc{.address = "0x" + std::string(40, '0'),
        .balance = u256(0),
        .nonce = "0",
        .code = "0x",
        .storage = std::move(storage)});
    return genesis;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(GenesisImmutableHashTest)

BOOST_AUTO_TEST_CASE(SameInputSameHash)
{
    auto a = baseConfig();
    auto b = baseConfig();
    BOOST_CHECK_EQUAL(computeGenesisImmutableHash(a).hex(), computeGenesisImmutableHash(b).hex());
}

BOOST_AUTO_TEST_CASE(ModeChangeChangesHash)
{
    auto l2 = baseConfig();
    auto pbft = baseConfig();
    pbft.m_chainMode = "pbft";
    pbft.m_allocs.clear();  // pbft variant must have empty allocs (NodeConfig rule)
    BOOST_CHECK_NE(computeGenesisImmutableHash(l2).hex(), computeGenesisImmutableHash(pbft).hex());
}

BOOST_AUTO_TEST_CASE(AllocOrderIndependent)
{
    Alloc allocA{.address = "0x" + std::string(39, '0') + "1",
        .balance = u256(10),
        .nonce = "0",
        .code = "0x",
        .storage = {}};
    Alloc allocB{.address = "0x" + std::string(39, '0') + "2",
        .balance = u256(20),
        .nonce = "0",
        .code = "0x",
        .storage = {}};

    GenesisConfig forward = baseConfig();
    forward.m_allocs.clear();
    forward.m_allocs.push_back(allocA);
    forward.m_allocs.push_back(allocB);

    GenesisConfig reversed = baseConfig();
    reversed.m_allocs.clear();
    reversed.m_allocs.push_back(allocB);
    reversed.m_allocs.push_back(allocA);

    BOOST_CHECK_EQUAL(
        computeGenesisImmutableHash(forward).hex(), computeGenesisImmutableHash(reversed).hex());
}

BOOST_AUTO_TEST_CASE(SingleByteFlipChangesHash)
{
    auto a = baseConfig();
    auto b = baseConfig();
    b.m_allocs[0].code = "0xab";  // was "0xaa"
    BOOST_CHECK_NE(computeGenesisImmutableHash(a).hex(), computeGenesisImmutableHash(b).hex());
}

// Regression for the value/next-key boundary collision: before storage values
// were length-prefixed, the two configs in each pair below hashed identically
// because the RAW value bytes flowed into the next key with no delimiter.
BOOST_AUTO_TEST_CASE(StorageValueBoundaryDistinct)
{
    // P1: {k0 -> "" , k1 -> 0x11x32B} vs {k0 -> 0x11x32B, k1 -> ""}
    auto p1a = configWithStorage({{key32('0'), "0x"}, {key32('1'), valueBytes('1', 1)}});
    auto p1b = configWithStorage({{key32('0'), valueBytes('1', 1)}, {key32('1'), "0x"}});
    BOOST_CHECK_NE(computeGenesisImmutableHash(p1a).hex(), computeGenesisImmutableHash(p1b).hex());

    // P2: {k0 -> 0x22x32B, k2 -> 0x22x64B} vs {k0 -> 0x22x64B, k2 -> 0x22x32B}
    auto p2a =
        configWithStorage({{key32('0'), valueBytes('2', 1)}, {key32('2'), valueBytes('2', 2)}});
    auto p2b =
        configWithStorage({{key32('0'), valueBytes('2', 2)}, {key32('2'), valueBytes('2', 1)}});
    BOOST_CHECK_NE(computeGenesisImmutableHash(p2a).hex(), computeGenesisImmutableHash(p2b).hex());
}

// GOLDEN: serialization is frozen; if this fails you broke chain-start for every
// existing L2 deployment. The literal below was produced once by the fixed
// implementation against the fixed config in this test.
BOOST_AUTO_TEST_CASE(GoldenVector)
{
    GenesisConfig genesis;
    genesis.m_chainMode = "l2";
    genesis.m_chainID = "901";
    genesis.m_groupID = "group0";
    genesis.m_smCrypto = false;
    genesis.m_isWasm = false;
    genesis.m_allocs.push_back(Alloc{.address = "0x42000000000000000000000000000000000000c0",
        .balance = u256(0),
        .nonce = "0",
        .code = "0x6080604052",
        .storage = {{"0x" + std::string(64, '0'), "0x" + std::string(60, '0') + "0385"}}});
    BOOST_CHECK_EQUAL(computeGenesisImmutableHash(genesis).hex(),
        "114cc991e4957cc99d51a4c63481bee9f2c2084409bc7bdce4d637d2fb0e0257");
}

// nonce that overflows uint64 must abort hashing with a clear error.
BOOST_AUTO_TEST_CASE(NonceOverflowThrows)
{
    auto genesis = baseConfig();
    genesis.m_allocs[0].nonce = "99999999999999999999999";  // > 2^64-1
    BOOST_CHECK_THROW(computeGenesisImmutableHash(genesis), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
