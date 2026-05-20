/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file New1_Web3TxHashCanonicalTest.cpp
 * @brief FIB-New1: Web3 canonical txHash recompute in Transaction::verify.
 *
 * Covers 12 scenarios; see test-case docstrings.
 */

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-codec/web3/Web3Transaction.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <stdexcept>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{

// Use a fixed deterministic private key so tests are stable.
constexpr std::string_view PRIV_HEX =
    "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

// Build a Web3Transaction skeleton of the requested EIP type, signed by PRIV_HEX.
// After this returns, w.signatureR / w.signatureS / w.signatureV are populated
// and w.txHash() produces the canonical wire-form hash.
rpc::Web3Transaction makeSignedWeb3(rpc::TransactionType type, uint64_t chainId)
{
    rpc::Web3Transaction w;
    w.type = type;
    w.chainId = chainId;
    w.nonce = 7;
    w.gasLimit = 21000;
    w.maxPriorityFeePerGas = u256("1000000000");
    w.maxFeePerGas = u256("2000000000");
    w.value = u256("1000000000000000");
    w.to = Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a");
    w.data = {0xde, 0xad, 0xbe, 0xef};

    auto signingPayload = w.encodeForSign();
    auto signingHash = bcos::crypto::keccak256Hash(bcos::ref(signingPayload));

    Secp256k1Crypto sig;
    auto key = std::make_shared<KeyImpl>(fromHex(PRIV_HEX));
    auto keyPair = std::make_unique<Secp256k1KeyPair>(key);
    auto signature = sig.sign(*keyPair, signingHash, /*addHashFlag=*/false);
    BOOST_REQUIRE_EQUAL(signature->size(), 65u);
    w.signatureR.assign(signature->begin(), signature->begin() + 32);
    w.signatureS.assign(signature->begin() + 32, signature->begin() + 64);
    w.signatureV = static_cast<uint64_t>(signature->back());
    return w;
}

// Build a tars Transaction equivalent to what EthEndpoint::sendRawTransaction
// hands to verify() -- with an OPTIONAL prewritten canonical extraTransactionHash.
bcostars::Transaction buildTarsFromWeb3(const rpc::Web3Transaction& w, bool prewriteCanonicalHash)
{
    bcostars::Transaction t{};
    t.data.chainID = std::to_string(w.chainId.value_or(0));
    t.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);

    auto signingPayload = w.encodeForSign();
    t.extraTransactionBytes.assign(signingPayload.begin(), signingPayload.end());

    BOOST_REQUIRE_EQUAL(w.signatureR.size(), 32u);
    BOOST_REQUIRE_EQUAL(w.signatureS.size(), 32u);
    t.signature.reserve(65);
    t.signature.insert(t.signature.end(), w.signatureR.begin(), w.signatureR.end());
    t.signature.insert(t.signature.end(), w.signatureS.begin(), w.signatureS.end());
    t.signature.push_back(static_cast<tars::Char>(w.signatureV));

    if (prewriteCanonicalHash)
    {
        auto h = w.txHash();
        t.extraTransactionHash.assign(h.begin(), h.end());
    }
    return t;
}

std::shared_ptr<bcostars::protocol::TransactionImpl> wrap(bcostars::Transaction&& t)
{
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = std::move(t)]() mutable { return std::addressof(inner); });
}

}  // namespace

struct New1Fixture
{
    Keccak256 hashImpl;
    Secp256k1Crypto signatureImpl;
};

BOOST_FIXTURE_TEST_SUITE(New1Web3TxHashCanonicalTest, New1Fixture)

// N1_T1: Legacy RPC clean path -- prewritten canonical hash is honoured; tx->hash() = canonical
BOOST_AUTO_TEST_CASE(N1_T1_legacy_rpc_clean)
{
    auto w = makeSignedWeb3(rpc::TransactionType::Legacy, /*chainId=*/1);
    auto canonical = w.txHash();
    auto tx = wrap(buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/true));
    BOOST_CHECK(tx->tainted());
    tx->verify(hashImpl, signatureImpl);
    BOOST_CHECK(!tx->tainted());
    BOOST_CHECK_EQUAL(tx->hash(), canonical);
    BOOST_CHECK(!tx->sender().empty());
}

// N1_T2: Legacy P2P import with FAKE wire hash -- clearSenderAndHash drops it,
//        verify recomputes canonical and tx->hash() returns the canonical value.
BOOST_AUTO_TEST_CASE(N1_T2_legacy_p2p_overwrites_fake_hash)
{
    auto w = makeSignedWeb3(rpc::TransactionType::Legacy, /*chainId=*/1);
    auto canonical = w.txHash();
    auto tars = buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/false);
    bcos::bytes fake(32, 0xab);
    tars.extraTransactionHash.assign(fake.begin(), fake.end());
    auto tx = wrap(std::move(tars));
    tx->clearSenderAndHash();
    tx->verify(hashImpl, signatureImpl);
    BOOST_CHECK_EQUAL(tx->hash(), canonical);
    BOOST_CHECK_NE(tx->hash(), HashType(fake.data(), fake.size()));
}

// N1_T3: EIP-1559 clean RPC path
BOOST_AUTO_TEST_CASE(N1_T3_eip1559_rpc_clean)
{
    auto w = makeSignedWeb3(rpc::TransactionType::EIP1559, /*chainId=*/5);
    auto canonical = w.txHash();
    auto tx = wrap(buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/true));
    tx->verify(hashImpl, signatureImpl);
    BOOST_CHECK_EQUAL(tx->hash(), canonical);
}

// N1_T4: EIP-1559 P2P with fake hash overwritten
BOOST_AUTO_TEST_CASE(N1_T4_eip1559_p2p_overwrites_fake_hash)
{
    auto w = makeSignedWeb3(rpc::TransactionType::EIP1559, /*chainId=*/5);
    auto canonical = w.txHash();
    auto tars = buildTarsFromWeb3(w, false);
    bcos::bytes fake(32, 0xcd);
    tars.extraTransactionHash.assign(fake.begin(), fake.end());
    auto tx = wrap(std::move(tars));
    tx->clearSenderAndHash();
    tx->verify(hashImpl, signatureImpl);
    BOOST_CHECK_EQUAL(tx->hash(), canonical);
}

// N1_T5: EIP-2930 P2P with fake hash overwritten
BOOST_AUTO_TEST_CASE(N1_T5_eip2930_p2p_overwrites_fake_hash)
{
    auto w = makeSignedWeb3(rpc::TransactionType::EIP2930, /*chainId=*/5);
    auto canonical = w.txHash();
    auto tars = buildTarsFromWeb3(w, false);
    bcos::bytes fake(32, 0xef);
    tars.extraTransactionHash.assign(fake.begin(), fake.end());
    auto tx = wrap(std::move(tars));
    tx->clearSenderAndHash();
    tx->verify(hashImpl, signatureImpl);
    BOOST_CHECK_EQUAL(tx->hash(), canonical);
}

// N1_T6: tainted=false short-circuits verify. After a successful first verify,
// poking extraTransactionHash and calling verify() again does NOT overwrite.
BOOST_AUTO_TEST_CASE(N1_T6_tainted_false_skips_recompute)
{
    auto w = makeSignedWeb3(rpc::TransactionType::Legacy, /*chainId=*/1);
    auto tx = wrap(buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/true));
    tx->verify(hashImpl, signatureImpl);
    BOOST_REQUIRE(!tx->tainted());

    auto& inner = tx->mutableInner();
    bcos::bytes poison(32, 0x77);
    inner.extraTransactionHash.assign(poison.begin(), poison.end());
    tx->verify(hashImpl, signatureImpl);
    BOOST_CHECK_EQUAL_COLLECTIONS(inner.extraTransactionHash.begin(),
        inner.extraTransactionHash.end(), poison.begin(), poison.end());
}

// N1_T7: Signature recovery failure -> exception escapes verify;
// extraTransactionHash stays empty. We force failure by zeroing r (r==0 is
// outside the secp256k1 group and is rejected by secp256k1_ecdsa_recover).
BOOST_AUTO_TEST_CASE(N1_T7_signature_recover_failure_throws)
{
    auto w = makeSignedWeb3(rpc::TransactionType::Legacy, /*chainId=*/1);
    auto tars = buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/false);
    std::fill(tars.signature.begin(), tars.signature.begin() + 32, '\0');
    auto tx = wrap(std::move(tars));
    BOOST_CHECK_THROW(tx->verify(hashImpl, signatureImpl), std::exception);
    BOOST_CHECK(tx->mutableInner().extraTransactionHash.empty());
}

// N1_T8: dataHash invariant -- after verify, dataHash = keccak256(extraTransactionBytes).
BOOST_AUTO_TEST_CASE(N1_T8_datahash_populated_post_verify)
{
    auto w = makeSignedWeb3(rpc::TransactionType::Legacy, /*chainId=*/1);
    auto tars = buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/true);
    BOOST_REQUIRE(tars.dataHash.empty());
    auto tx = wrap(std::move(tars));
    tx->verify(hashImpl, signatureImpl);
    auto const& inner = tx->inner();
    BOOST_REQUIRE_EQUAL(inner.dataHash.size(), 32u);
    auto expectedSigningHash = bcos::crypto::keccak256Hash(tx->extraTransactionBytes());
    BOOST_CHECK_EQUAL_COLLECTIONS(inner.dataHash.begin(), inner.dataHash.end(),
        reinterpret_cast<const char*>(expectedSigningHash.begin()),
        reinterpret_cast<const char*>(expectedSigningHash.end()));
}

// N1_T9: BCOSTransaction (type=0) unaffected -- verify goes through the base-class path
// (which throws because the synthesized tx has no usable signature). The point is that
// the Web3 recompute branch is never reached.
BOOST_AUTO_TEST_CASE(N1_T9_bcos_tx_unaffected)
{
    bcostars::Transaction t{};
    t.type = static_cast<tars::Char>(bcos::protocol::TransactionType::BCOSTransaction);
    t.data.nonce = "1";
    t.data.chainID = "testChain";
    t.data.groupID = "testGroup";
    auto tx = wrap(std::move(t));
    BOOST_CHECK_THROW(tx->verify(hashImpl, signatureImpl), std::exception);
}

// N1_T10: Forged extraTransactionBytes (mutated payload, original signature). Either:
//   (a) recover yields a DIFFERENT address (no exception, sender mismatches the real one), or
//   (b) recover fails entirely -> exception.
BOOST_AUTO_TEST_CASE(N1_T10_forged_bytes_changes_sender_or_throws)
{
    auto w = makeSignedWeb3(rpc::TransactionType::Legacy, /*chainId=*/1);

    // Compute the "real" sender via direct recovery on the untouched payload.
    auto signingHash = bcos::crypto::keccak256Hash(bcos::ref(w.encodeForSign()));
    bcos::bytes realSig;
    realSig.reserve(65);
    realSig.insert(realSig.end(), w.signatureR.begin(), w.signatureR.end());
    realSig.insert(realSig.end(), w.signatureS.begin(), w.signatureS.end());
    realSig.push_back(static_cast<bcos::byte>(w.signatureV));
    auto [okReal, realSender] =
        signatureImpl.recoverAddress(hashImpl, signingHash, bcos::ref(realSig));
    BOOST_REQUIRE(okReal);

    auto tars = buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/false);
    BOOST_REQUIRE_GT(tars.extraTransactionBytes.size(), 10u);
    tars.extraTransactionBytes[5] ^= 0x01;
    auto tx = wrap(std::move(tars));
    tx->clearSenderAndHash();
    bool threw = false;
    try
    {
        tx->verify(hashImpl, signatureImpl);
    }
    catch (std::exception const&)
    {
        threw = true;
    }
    if (!threw)
    {
        // recover succeeded on tampered payload -> recovered sender must differ
        bcos::bytes recoveredBytes(tx->sender().begin(), tx->sender().end());
        BOOST_CHECK(recoveredBytes != realSender);
    }
}

// N1_T11: Malformed extraTransactionBytes (garbage) -> decodeFromPayload fails or
//         recover fails. Either way an exception escapes verify().
BOOST_AUTO_TEST_CASE(N1_T11_malformed_bytes_throws)
{
    auto w = makeSignedWeb3(rpc::TransactionType::Legacy, /*chainId=*/1);
    auto tars = buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/false);
    tars.extraTransactionBytes.assign({static_cast<char>(0xff), static_cast<char>(0xff),
        static_cast<char>(0xff), static_cast<char>(0xff)});
    auto tx = wrap(std::move(tars));
    tx->clearSenderAndHash();
    BOOST_CHECK_THROW(tx->verify(hashImpl, signatureImpl), std::exception);
}

// N1_T12: Empty signature -> verify throws.
BOOST_AUTO_TEST_CASE(N1_T12_empty_signature_throws)
{
    auto w = makeSignedWeb3(rpc::TransactionType::Legacy, /*chainId=*/1);
    auto tars = buildTarsFromWeb3(w, /*prewriteCanonicalHash=*/false);
    tars.signature.clear();
    auto tx = wrap(std::move(tars));
    tx->clearSenderAndHash();
    BOOST_CHECK_THROW(tx->verify(hashImpl, signatureImpl), std::exception);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
