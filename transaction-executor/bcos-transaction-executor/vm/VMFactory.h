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
 * @brief factory of vm
 * @file VMFactory.h
 * @author: ancelmo
 * @date: 2022-12-15
 */

#pragma once
#include "VMInstance.h"
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <boost/compute/detail/lru_cache.hpp>
#include <boost/throw_exception.hpp>
#include <evmone/baseline.hpp>
#include <memory>
#include <mutex>

namespace bcos::executor_v1
{

constexpr std::size_t c_EVMONE_CACHE_SIZE = 1024;

enum class VMKind
{
    evmone,
};

DERIVE_BCOS_EXCEPTION(UnknownVMError);

/// FIB-93: stateful VM factory holding a single-revision LRU cache of
/// evmone CodeAnalysis keyed by code hash. Mirrors the strategy of the non-v1
/// executor in bcos-executor/src/vm/VMFactory: cache entries belong to ONE
/// EVMC revision; inserting with a different revision clears the cache.
/// Sits BENEATH HostContext's address-keyed Executable cache: identical
/// bytecode at distinct addresses (clones / proxies / redeployments) reuses
/// the same CodeAnalysis instead of re-running evmone::baseline::analyze.
class VMFactory
{
public:
    explicit VMFactory(std::size_t cacheSize = c_EVMONE_CACHE_SIZE);

    /// Returns a VMInstance wrapping a (possibly cached) CodeAnalysis.
    /// isCreate=true bypasses the cache (mirrors non-v1 VMFactory.cpp:60).
    VMInstance create(VMKind kind, bytesConstRef code, evmc_revision mode, bool isCreate = false);

    /// Test-only accessor for the current cache occupancy. NOT for production.
    std::size_t testOnlyCacheSize() const noexcept;

private:
    std::shared_ptr<evmone::baseline::CodeAnalysis const> getOrCompute(
        const bcos::crypto::HashType& key, evmc_revision revision, bytesConstRef code);

    boost::compute::detail::lru_cache<bcos::crypto::HashType,
        std::shared_ptr<evmone::baseline::CodeAnalysis const>>
        m_cache;
    evmc_revision m_revision = EVMC_PARIS;
    mutable std::mutex m_cacheMutex;
};

}  // namespace bcos::executor_v1
