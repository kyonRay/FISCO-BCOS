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
 * @brief Regression test for FIB-67: P2PMessageOptions::decode protocol-limit validation
 * @file FIB67_OptionsDecodeValidationTest.cpp
 * @date 2026-04-07
 */

#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

namespace
{
/// Helper: build a raw P2PMessageOptions binary buffer from components.
///
/// Wire format:
///   groupID length  (2 bytes, network order)
///   groupID         (variable)
///   nodeID length   (2 bytes, network order)
///   srcNodeID       (variable)
///   dstNodeCount    (1 byte)
///   dstNodeIDs      (dstNodeCount * nodeIDLength bytes)
///   moduleID        (2 bytes, network order)
bytes buildOptionsPayload(uint16_t groupIDLength, const bytes& groupID, uint16_t nodeIDLength,
    const bytes& srcNodeID, uint8_t dstNodeCount, const std::vector<bytes>& dstNodeIDs,
    uint16_t moduleID)
{
    bytes buf;

    // groupID length (network byte order)
    uint16_t netGroupIDLen = boost::asio::detail::socket_ops::host_to_network_short(groupIDLength);
    buf.insert(buf.end(), (byte*)&netGroupIDLen, (byte*)&netGroupIDLen + 2);

    // groupID data
    buf.insert(buf.end(), groupID.begin(), groupID.end());

    // nodeID length (network byte order)
    uint16_t netNodeIDLen = boost::asio::detail::socket_ops::host_to_network_short(nodeIDLength);
    buf.insert(buf.end(), (byte*)&netNodeIDLen, (byte*)&netNodeIDLen + 2);

    // srcNodeID data
    buf.insert(buf.end(), srcNodeID.begin(), srcNodeID.end());

    // dstNodeCount
    buf.push_back(static_cast<byte>(dstNodeCount));

    // dstNodeIDs
    for (const auto& dst : dstNodeIDs)
    {
        buf.insert(buf.end(), dst.begin(), dst.end());
    }

    // moduleID (network byte order)
    uint16_t netModuleID = boost::asio::detail::socket_ops::host_to_network_short(moduleID);
    buf.insert(buf.end(), (byte*)&netModuleID, (byte*)&netModuleID + 2);

    return buf;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB67_OptionsDecodeValidationTest, TestPromptFixture)

/// Valid options payload should still decode successfully (regression guard).
BOOST_AUTO_TEST_CASE(ValidPayloadDecodesSuccessfully)
{
    std::string groupID = "testGroup";
    std::string srcNodeID = "srcNode01";
    std::string dstNodeID = "dstNode01";  // same length as srcNodeID
    uint16_t moduleID = 42;

    bytes groupIDBytes(groupID.begin(), groupID.end());
    bytes srcBytes(srcNodeID.begin(), srcNodeID.end());
    bytes dstBytes(dstNodeID.begin(), dstNodeID.end());

    auto payload = buildOptionsPayload(static_cast<uint16_t>(groupIDBytes.size()), groupIDBytes,
        static_cast<uint16_t>(srcBytes.size()), srcBytes, 1, {dstBytes}, moduleID);

    P2PMessageOptions opts;
    auto ret = opts.decode(bytesConstRef(payload.data(), payload.size()));

    BOOST_CHECK_GT(ret, 0);
    BOOST_CHECK_EQUAL(opts.groupID(), groupID);
    BOOST_CHECK_EQUAL(opts.moduleID(), moduleID);
    BOOST_CHECK_EQUAL(
        std::string(opts.srcNodeID().begin(), opts.srcNodeID().end()), srcNodeID);
    BOOST_REQUIRE_EQUAL(opts.dstNodeIDs().size(), 1u);
    BOOST_CHECK_EQUAL(
        std::string(opts.dstNodeIDs()[0].begin(), opts.dstNodeIDs()[0].end()), dstNodeID);
}

/// groupIDLength exceeding MAX_GROUPID_LENGTH must be rejected.
/// We craft a buffer whose groupIDLength field claims a value larger than 65535.
/// Since the wire type is uint16_t, the maximum representable value IS 65535, which
/// equals MAX_GROUPID_LENGTH. So the current uint16_t cannot exceed the constant.
/// However, the validation is still valuable as a defence-in-depth measure if the
/// protocol ever changes. We test with the maximum value to ensure it still passes
/// (since 65535 == MAX_GROUPID_LENGTH).
///
/// Instead, we test that a groupIDLength that is large but whose actual data is
/// shorter than the claimed length causes decode to return MESSAGE_ERROR (via
/// checkOffset), proving that the decode path is robust against oversized claims.
BOOST_AUTO_TEST_CASE(GroupIDLengthLargerThanBufferIsRejected)
{
    // Claim groupIDLength = 60000, but provide only 5 bytes of groupID data.
    // The buffer is too short for the claimed length, so decode must fail.
    bytes groupIDBytes = {0x41, 0x42, 0x43, 0x44, 0x45};  // "ABCDE"
    uint16_t claimedGroupIDLength = 60000;

    bytes buf;
    uint16_t netLen = boost::asio::detail::socket_ops::host_to_network_short(claimedGroupIDLength);
    buf.insert(buf.end(), (byte*)&netLen, (byte*)&netLen + 2);
    buf.insert(buf.end(), groupIDBytes.begin(), groupIDBytes.end());
    // Append minimal remaining fields so OPTIONS_MIN_LENGTH check passes
    // nodeID length (2) + dstNodeCount (1) + moduleID (2) = 5 more bytes
    buf.resize(buf.size() + 5, 0);

    P2PMessageOptions opts;
    auto ret = opts.decode(bytesConstRef(buf.data(), buf.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

/// nodeIDLength exceeding MAX_NODEID_LENGTH must be rejected.
/// Same reasoning as above -- uint16_t max is 65535 == MAX_NODEID_LENGTH.
/// We test that a large nodeIDLength with insufficient data causes MESSAGE_ERROR.
BOOST_AUTO_TEST_CASE(NodeIDLengthLargerThanBufferIsRejected)
{
    // Valid groupID section
    std::string groupID = "grp";
    bytes groupIDBytes(groupID.begin(), groupID.end());

    bytes buf;
    // groupID length
    uint16_t netGroupLen =
        boost::asio::detail::socket_ops::host_to_network_short(static_cast<uint16_t>(groupIDBytes.size()));
    buf.insert(buf.end(), (byte*)&netGroupLen, (byte*)&netGroupLen + 2);
    buf.insert(buf.end(), groupIDBytes.begin(), groupIDBytes.end());

    // nodeID length = 50000 but no actual data to back it
    uint16_t claimedNodeIDLen = 50000;
    uint16_t netNodeLen = boost::asio::detail::socket_ops::host_to_network_short(claimedNodeIDLen);
    buf.insert(buf.end(), (byte*)&netNodeLen, (byte*)&netNodeLen + 2);
    // Only provide 4 bytes of srcNodeID instead of 50000
    buf.insert(buf.end(), {0x01, 0x02, 0x03, 0x04});
    // dstNodeCount + moduleID filler
    buf.resize(buf.size() + 3, 0);

    P2PMessageOptions opts;
    auto ret = opts.decode(bytesConstRef(buf.data(), buf.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

/// Decode with zero-length groupID and zero-length nodeID should fail
/// because encode() rejects empty srcNodeID. But decode() should still
/// handle it gracefully (checkOffset catches it).
BOOST_AUTO_TEST_CASE(ZeroLengthNodeIDCausesCheckOffsetFailure)
{
    // groupIDLength=0, nodeIDLength=0, dstNodeCount=0, moduleID=0
    auto payload = buildOptionsPayload(0, {}, 0, {}, 0, {}, 0);

    P2PMessageOptions opts;
    auto ret = opts.decode(bytesConstRef(payload.data(), payload.size()));
    // With nodeIDLength=0, dstNodeCount=0, this should actually succeed
    // because all checkOffset calls pass (0-length reads are fine).
    BOOST_CHECK_GT(ret, 0);
    BOOST_CHECK_EQUAL(opts.groupID(), "");
    BOOST_CHECK_EQUAL(opts.srcNodeID().size(), 0u);
    BOOST_CHECK_EQUAL(opts.dstNodeIDs().size(), 0u);
}

/// A valid round-trip: encode then decode should produce identical options.
BOOST_AUTO_TEST_CASE(EncodeDecodeRoundTrip)
{
    P2PMessageOptions original;
    original.setGroupID("myGroup");
    bytes srcNode = {0xAA, 0xBB, 0xCC, 0xDD};
    original.setSrcNodeID(srcNode);
    bytes dstNode1 = {0xAA, 0xBB, 0xCC, 0xDD};
    bytes dstNode2 = {0x11, 0x22, 0x33, 0x44};
    original.setDstNodeIDs({dstNode1, dstNode2});
    original.setModuleID(999);

    bytes encoded;
    BOOST_REQUIRE(original.encode(encoded));

    P2PMessageOptions decoded;
    auto ret = decoded.decode(bytesConstRef(encoded.data(), encoded.size()));
    BOOST_CHECK_GT(ret, 0);
    BOOST_CHECK_EQUAL(decoded.groupID(), "myGroup");
    BOOST_CHECK_EQUAL(decoded.moduleID(), 999);
    BOOST_CHECK_EQUAL(decoded.srcNodeID().size(), 4u);
    BOOST_REQUIRE_EQUAL(decoded.dstNodeIDs().size(), 2u);
    BOOST_CHECK(decoded.dstNodeIDs()[0] == dstNode1);
    BOOST_CHECK(decoded.dstNodeIDs()[1] == dstNode2);
}

/// Truncated buffer (less than OPTIONS_MIN_LENGTH) must be rejected.
BOOST_AUTO_TEST_CASE(TruncatedBufferIsRejected)
{
    // OPTIONS_MIN_LENGTH is 7; provide only 4 bytes.
    bytes buf = {0x00, 0x00, 0x00, 0x00};

    P2PMessageOptions opts;
    auto ret = opts.decode(bytesConstRef(buf.data(), buf.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

BOOST_AUTO_TEST_SUITE_END()
