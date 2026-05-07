/**
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
 * @brief regression tests for FIB-120 and FIB-123: PBFTProposal::decode()
 *        must reject repeated fields that exceed MAX_PBFT_REPEATED_FIELD_SIZE
 * @file FIB120_PBFTProposalDecodeBoundsTest.cpp
 * @author: kyonRay
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;

namespace bcos
{
namespace test
{

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// Build a serialised PBFTRawProposal with `sigCount` signaturelist entries
/// and `nodeCount` nodelist entries.  All entries are trivially sized.
static bytes makePBFTProposalBytes(int sigCount, int nodeCount)
{
    PBFTRawProposal raw;
    // fill a minimal inner proposal so decodePBObject succeeds
    raw.mutable_proposal()->set_index(1);
    raw.mutable_proposal()->set_hash("hash");

    for (int i = 0; i < sigCount; ++i)
    {
        raw.add_signaturelist("sig");
    }
    for (int i = 0; i < nodeCount; ++i)
    {
        raw.add_nodelist(static_cast<int64_t>(i));
    }

    std::string serialised = raw.SerializeAsString();
    return bytes(serialised.begin(), serialised.end());
}

// ---------------------------------------------------------------------------
// test suite
// ---------------------------------------------------------------------------

BOOST_FIXTURE_TEST_SUITE(FIB120_PBFTProposalDecodeBounds, TestPromptFixture)

/// FIB-120: signatureList with more than MAX_PBFT_REPEATED_FIELD_SIZE entries
/// must be rejected by PBFTProposal::decode().
BOOST_AUTO_TEST_CASE(decode_rejects_excessive_signatureList)
{
    // 100001 > MAX_PBFT_REPEATED_FIELD_SIZE (100000) — must throw
    auto data = makePBFTProposalBytes(100001, 100001);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

/// Normal-sized lists (100 entries each) must be accepted without exception.
BOOST_AUTO_TEST_CASE(decode_accepts_normal_signatureList)
{
    auto data = makePBFTProposalBytes(100, 100);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_NO_THROW(PBFTProposal{ref});
}

/// FIB-123: nodeList with more than MAX_PBFT_REPEATED_FIELD_SIZE entries
/// must be rejected by PBFTProposal::decode().
BOOST_AUTO_TEST_CASE(decode_rejects_excessive_nodeList_FIB123)
{
    // balanced counts: signatureList is within bound, nodeList overflows
    auto data = makePBFTProposalBytes(1, 100001);
    bytesConstRef ref(data.data(), data.size());
    BOOST_CHECK_THROW(PBFTProposal{ref}, bcos::Exception);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
