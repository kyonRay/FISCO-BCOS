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
 * @file GenesisImmutableHash.h
 * @brief Canonical, cross-platform hash over the genesis fields that must never
 *        change after the first init (chain mode, chainID, groupID, smCrypto,
 *        isWasm, allocs). Stored on first init as `genesis_immutable_hash` and
 *        re-verified on every subsequent startup.
 */
#pragma once
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-utilities/FixedBytes.h>

namespace bcos::ledger
{
// Deterministic keccak256 over the immutable genesis input domain.
// Order-independent w.r.t. alloc push order and per-alloc storage-slot order
// (both are sorted before hashing).
//
// CANONICAL BYTE LAYOUT (frozen; see GenesisImmutableHash.cpp for the encoder):
//   chainMode    : bytes + 0x00 terminator
//   chainID      : bytes + 0x00 terminator
//   groupID      : bytes + 0x00 terminator
//   smCrypto     : 1 byte (0|1)
//   isWasm       : 1 byte (0|1)
//   0x00         : header/allocs separator
//   allocCount   : BE32
//   per alloc (sorted by address):
//     address    : 20 bytes (fixed width, no length prefix)
//     balance    : 32 bytes BE (u256)
//     nonce      : 8 bytes BE (uint64)
//     codeHash   : 32 bytes (keccak256 of code bytes)
//     slotCount  : BE32
//     per slot (sorted by key):
//       key      : 32 bytes (fixed width, no length prefix)
//       valueLen : BE32   <-- length prefix: value is variable width
//       value    : valueLen bytes
// Fixed-width fields (address, key) carry no prefix because NodeConfig validation
// pins their length; the variable-width storage value is length-prefixed so the
// value/next-key boundary cannot shift (was a hash-collision vector before PR-3).
bcos::h256 computeGenesisImmutableHash(GenesisConfig const& genesis);
}  // namespace bcos::ledger
