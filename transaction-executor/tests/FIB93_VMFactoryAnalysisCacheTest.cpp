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
 * @brief FIB-93 unit tests for VMFactory analysis cache.
 *        Same code at the same revision reuses analysis (cache hit). Distinct
 *        code yields distinct analyses. Cache is single-revision: switching
 *        revision clears the cache (mirrors non-v1 VMFactory.cpp:99).
 *        isCreate path bypasses the cache.
 */

#include "../bcos-transaction-executor/vm/VMFactory.h"
#include "../bcos-transaction-executor/vm/VMInstance.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>
#include <vector>

namespace bcos::test
{
namespace
{

inline bcos::bytes makeBytecodeFib93(int seed)
{
    // 64-byte deterministic payload; varying seed yields different code hashes.
    bcos::bytes code;
    code.resize(64);
    for (int i = 0; i < 64; ++i)
    {
        code[i] = static_cast<bcos::byte>(seed + i);
    }
    return code;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(FIB93VMFactoryAnalysisCacheTest)

BOOST_AUTO_TEST_CASE(same_code_same_revision_cache_hit)
{
    bcos::executor_v1::VMFactory factory;
    auto code = makeBytecodeFib93(1);
    auto vm1 = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(code.data(), code.size()), EVMC_PARIS, false);
    auto vm2 = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(code.data(), code.size()), EVMC_PARIS, false);
    // Cache hit: both VMInstances share the same CodeAnalysis pointer.
    BOOST_CHECK(vm1.analysisRawPtr() != nullptr);
    BOOST_CHECK_EQUAL(vm1.analysisRawPtr(), vm2.analysisRawPtr());
    BOOST_CHECK_EQUAL(factory.testOnlyCacheSize(), 1U);
}

BOOST_AUTO_TEST_CASE(different_code_distinct_analyses)
{
    bcos::executor_v1::VMFactory factory;
    auto codeA = makeBytecodeFib93(1);
    auto codeB = makeBytecodeFib93(2);
    auto vmA = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(codeA.data(), codeA.size()), EVMC_PARIS, false);
    auto vmB = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(codeB.data(), codeB.size()), EVMC_PARIS, false);
    BOOST_CHECK(vmA.analysisRawPtr() != vmB.analysisRawPtr());
    BOOST_CHECK_EQUAL(factory.testOnlyCacheSize(), 2U);
}

BOOST_AUTO_TEST_CASE(revision_change_clears_cache)
{
    // Mirrors non-v1 behavior (bcos-executor/src/vm/VMFactory.cpp:99): cache holds
    // entries for ONE revision; insert with a different revision clears the cache.
    // Hence the same code at a different revision results in a fresh analysis,
    // and going back to the original revision after the switch ALSO re-analyzes
    // because the prior PARIS entry was evicted by the SHANGHAI insert.
    bcos::executor_v1::VMFactory factory;
    auto code = makeBytecodeFib93(1);
    auto vmParis = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(code.data(), code.size()), EVMC_PARIS, false);
    auto vmShang = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(code.data(), code.size()), EVMC_SHANGHAI, false);
    BOOST_CHECK(vmParis.analysisRawPtr() != vmShang.analysisRawPtr());
    // After SHANGHAI insert, cache only holds SHANGHAI entries.
    BOOST_CHECK_EQUAL(factory.testOnlyCacheSize(), 1U);

    // PARIS lookup re-analyzes (different from the original PARIS analysis).
    auto vmParis2 = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(code.data(), code.size()), EVMC_PARIS, false);
    BOOST_CHECK(vmParis2.analysisRawPtr() != vmParis.analysisRawPtr());
    BOOST_CHECK(vmParis2.analysisRawPtr() != vmShang.analysisRawPtr());
}

BOOST_AUTO_TEST_CASE(isCreate_bypasses_cache)
{
    bcos::executor_v1::VMFactory factory;
    auto code = makeBytecodeFib93(11);
    auto vm1 = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(code.data(), code.size()), EVMC_PARIS, /*isCreate=*/true);
    auto vm2 = factory.create(bcos::executor_v1::VMKind::evmone,
        bcos::bytesConstRef(code.data(), code.size()), EVMC_PARIS, /*isCreate=*/true);
    // isCreate path mirrors non-v1 semantics: bypass cache; each call freshly
    // analyzes. The two analyses are distinct heap objects.
    BOOST_CHECK(vm1.analysisRawPtr() != nullptr);
    BOOST_CHECK(vm2.analysisRawPtr() != nullptr);
    BOOST_CHECK(vm1.analysisRawPtr() != vm2.analysisRawPtr());
    // Cache untouched by isCreate path.
    BOOST_CHECK_EQUAL(factory.testOnlyCacheSize(), 0U);
}

BOOST_AUTO_TEST_CASE(concurrent_create_no_data_race)
{
    // Per Codex review: get/put pattern is race-tolerant — concurrent same-key
    // analyses produce equivalent results (last-writer-wins on insert). This UT
    // verifies the contract of "no crash + cache eventually populated"; the
    // stronger "only-once analyze" property is NOT promised. Run with TSan to
    // confirm no data race.
    bcos::executor_v1::VMFactory factory;
    auto code = makeBytecodeFib93(7);
    constexpr int N = 8;
    constexpr int ITER = 100;
    std::vector<std::thread> threads;
    threads.reserve(N);
    std::atomic<int> nonNullCount{0};
    for (int i = 0; i < N; ++i)
    {
        threads.emplace_back([&]() {
            for (int j = 0; j < ITER; ++j)
            {
                auto vm = factory.create(bcos::executor_v1::VMKind::evmone,
                    bcos::bytesConstRef(code.data(), code.size()), EVMC_PARIS, false);
                if (vm.analysisRawPtr() != nullptr)
                {
                    nonNullCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& t : threads)
    {
        t.join();
    }
    BOOST_CHECK_EQUAL(nonNullCount.load(), N * ITER);
    // Cache holds exactly one entry (single distinct code hash at single revision).
    BOOST_CHECK_EQUAL(factory.testOnlyCacheSize(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
