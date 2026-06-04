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
 * @file GenesisImmutableHash.cpp
 */
#include "GenesisImmutableHash.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::ledger;

namespace
{
// big-endian fixed-width append helpers (no platform-dependent memory layout)
void appendBE32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendBE64(std::vector<uint8_t>& out, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

// u256 as 32-byte big-endian (left-padded). FixedBytes<32>'s arith ctor stores
// big-endian into a fixed 32-byte buffer, which is exactly what we want.
void appendU256BE(std::vector<uint8_t>& out, u256 const& value)
{
    bcos::h256 fixed(value);
    auto ref = fixed.ref();
    out.insert(out.end(), ref.data(), ref.data() + ref.size());
}

// decode a hex string (optional 0x prefix, odd length left-padded) into raw bytes
bcos::bytes hexToBytes(std::string const& hex)
{
    return bcos::fromHex(hex);
}

// append a NUL-terminated string field
void appendString(std::vector<uint8_t>& out, std::string const& value)
{
    out.insert(out.end(), value.begin(), value.end());
    out.push_back(0x00);
}

// append a 20-byte address. Strict: the decoded form must be exactly 20 bytes.
// NodeConfig::loadAllocs already enforces a 40-hex-char (20-byte) address via
// requireHexField, so anything else here is a programming/input-domain error
// and must abort rather than silently truncate (truncation was a hash-collision
// vector when callers passed over-long addresses).
void appendAddress20(std::vector<uint8_t>& out, std::string const& addressHex)
{
    auto raw = hexToBytes(addressHex);
    constexpr size_t kAddressLen = 20;
    if (raw.size() != kAddressLen)
    {
        throw std::invalid_argument(
            "GenesisImmutableHash: alloc address must decode to exactly 20 bytes, got " +
            std::to_string(raw.size()) + " bytes for '" + addressHex + "'");
    }
    out.insert(out.end(), raw.begin(), raw.end());
}
}  // namespace

bcos::h256 bcos::ledger::computeGenesisImmutableHash(GenesisConfig const& genesis)
{
    std::vector<uint8_t> buffer;

    // -- scalar / string immutable fields --
    appendString(buffer, genesis.m_chainMode);
    appendString(buffer, genesis.m_chainID);
    appendString(buffer, genesis.m_groupID);
    buffer.push_back(genesis.m_smCrypto ? 1 : 0);
    buffer.push_back(genesis.m_isWasm ? 1 : 0);
    buffer.push_back(0x00);  // separator between header fields and allocs

    // -- allocs, sorted by address for order-independence --
    std::vector<Alloc const*> sortedAllocs;
    sortedAllocs.reserve(genesis.m_allocs.size());
    for (auto const& alloc : genesis.m_allocs)
    {
        sortedAllocs.push_back(&alloc);
    }
    std::sort(sortedAllocs.begin(), sortedAllocs.end(),
        [](Alloc const* lhs, Alloc const* rhs) { return lhs->address < rhs->address; });

    appendBE32(buffer, static_cast<uint32_t>(sortedAllocs.size()));

    for (auto const* allocPtr : sortedAllocs)
    {
        auto const& alloc = *allocPtr;

        // 20-byte address
        appendAddress20(buffer, alloc.address);

        // balance: u256 big-endian 32 bytes
        appendU256BE(buffer, alloc.balance);

        // nonce: parse decimal string -> uint64 big-endian (empty => 0).
        // std::from_chars never throws and reports overflow / non-numeric input
        // via its return code; surface either as a fatal, address-named error so
        // a malformed genesis aborts startup instead of silently hashing a
        // truncated value. (Do NOT pull in bcos-tool here; this is bcos-ledger.)
        uint64_t nonceValue = 0;
        if (!alloc.nonce.empty())
        {
            auto const* first = alloc.nonce.data();
            auto const* last = first + alloc.nonce.size();
            auto [ptr, ec] = std::from_chars(first, last, nonceValue);
            if (ec != std::errc{} || ptr != last)
            {
                throw std::invalid_argument(
                    "GenesisImmutableHash: alloc nonce is not a valid uint64 for address '" +
                    alloc.address + "': '" + alloc.nonce + "'");
            }
        }
        appendBE64(buffer, nonceValue);

        // keccak256(code bytes): 32 bytes (hash of empty bytes for empty code)
        auto codeBytes = hexToBytes(alloc.code);
        auto codeHash =
            crypto::keccak256Hash(bcos::bytesConstRef(codeBytes.data(), codeBytes.size()));
        auto codeHashRef = codeHash.ref();
        buffer.insert(buffer.end(), codeHashRef.data(), codeHashRef.data() + codeHashRef.size());

        // storage slots, sorted by key for order-independence
        std::vector<Alloc::State const*> sortedStorage;
        sortedStorage.reserve(alloc.storage.size());
        for (auto const& slot : alloc.storage)
        {
            sortedStorage.push_back(&slot);
        }
        std::sort(sortedStorage.begin(), sortedStorage.end(),
            [](Alloc::State const* lhs, Alloc::State const* rhs) {
                return lhs->first < rhs->first;
            });

        appendBE32(buffer, static_cast<uint32_t>(sortedStorage.size()));
        for (auto const* slotPtr : sortedStorage)
        {
            auto const& [slotHex, valueHex] = *slotPtr;
            auto slotBytes = hexToBytes(slotHex);
            auto valueBytes = hexToBytes(valueHex);
            // Key is fixed-width: NodeConfig::loadAllocs requires 64 hex chars
            // (32 bytes), so it is appended raw with no length prefix.
            buffer.insert(buffer.end(), slotBytes.begin(), slotBytes.end());
            // Value is VARIABLE width (a storage slot value may be any length),
            // so it MUST carry a BE32 length prefix. Without it the value/next-key
            // boundary can shift and two distinct storage maps collide to the same
            // hash (e.g. {k0->"", k1->32B} vs {k0->32B, k1->""}).
            appendBE32(buffer, static_cast<uint32_t>(valueBytes.size()));
            buffer.insert(buffer.end(), valueBytes.begin(), valueBytes.end());
        }
    }

    return crypto::keccak256Hash(bcos::bytesConstRef(buffer.data(), buffer.size()));
}
