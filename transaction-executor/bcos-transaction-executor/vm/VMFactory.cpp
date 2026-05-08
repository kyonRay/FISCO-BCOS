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
 * @file VMFactory.cpp
 * @author: ancelmo
 * @date: 2022-12-15
 */

#include "VMFactory.h"
#include "bcos-crypto/hash/Keccak256.h"
#include <evmone/baseline.hpp>

namespace bcos::executor_v1
{

VMFactory::VMFactory(std::size_t cacheSize) : m_cache(cacheSize) {}

VMInstance VMFactory::create(VMKind kind, bytesConstRef code, evmc_revision mode, bool isCreate)
{
    switch (kind)
    {
    case VMKind::evmone:
    {
        try
        {
            // FIB-93: matches non-v1 semantics (bcos-executor/src/vm/VMFactory.cpp:60) —
            // bypass code-hash cache for CREATE paths. Each CREATE gets a fresh analysis
            // because the bytecode is deployment input that may not be cacheable across
            // calls and conventionally wins no benefit from caching.
            if (isCreate)
            {
                auto analysis = std::make_shared<evmone::baseline::CodeAnalysis const>(
                    evmone::baseline::analyze(
                        mode, evmone::bytes_view(
                                  reinterpret_cast<const uint8_t*>(code.data()), code.size())));
                return VMInstance{std::move(analysis)};
            }
            return VMInstance{getOrCompute(bcos::crypto::keccak256Hash(code), mode, code)};
        }
        catch (const std::exception& e)
        {
            BCOS_LOG(ERROR) << LOG_BADGE("VM") << "Failed to analyze bytecode: " << e.what();
            BOOST_THROW_EXCEPTION(UnknownVMError{});
        }
    }
    default:
        BOOST_THROW_EXCEPTION(UnknownVMError{});
    }
}

std::shared_ptr<evmone::baseline::CodeAnalysis const> VMFactory::getOrCompute(
    const bcos::crypto::HashType& key, evmc_revision revision, bytesConstRef code)
{
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        // Mirror non-v1 strategy: only return a cache hit if revision matches.
        if (revision == m_revision)
        {
            if (auto entry = m_cache.get(key); entry)
            {
                return *entry;
            }
        }
    }

    // Compute outside the lock: race-tolerant per Codex review — concurrent
    // same-key analyses produce equivalent results, last-writer-wins on insert.
    auto analysis =
        std::make_shared<evmone::baseline::CodeAnalysis const>(evmone::baseline::analyze(revision,
            evmone::bytes_view(reinterpret_cast<const uint8_t*>(code.data()), code.size())));

    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (revision != m_revision)
        {
            // Mirror non-v1 (VMFactory.cpp:99): revision change clears the cache.
            m_cache.clear();
            m_revision = revision;
        }
        m_cache.insert(key, analysis);
    }
    return analysis;
}

std::size_t VMFactory::testOnlyCacheSize() const noexcept
{
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_cache.size();
}

}  // namespace bcos::executor_v1
