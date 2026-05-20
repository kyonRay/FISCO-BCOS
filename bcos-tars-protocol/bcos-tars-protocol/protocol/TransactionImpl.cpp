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
 * @brief tars implementation for Transaction
 * @file TransactionImpl.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */

#include "TransactionImpl.h"
#include "../impl/TarsHashable.h"
#include "../impl/TarsSerializable.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/web3/Web3Transaction.h>
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/endian/conversion.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <charconv>
#include <range/v3/view/any_view.hpp>
#include <stdexcept>

DERIVE_BCOS_EXCEPTION(EmptyTransactionHash);

bcostars::protocol::TransactionImpl::TransactionImpl(std::function<bcostars::Transaction*()> inner)
  : m_inner(std::move(inner))
{}
bcostars::protocol::TransactionImpl::TransactionImpl()
  : m_inner([m_transaction = bcostars::Transaction()]() mutable {
        return std::addressof(m_transaction);
    })
{}

bool bcostars::protocol::TransactionImpl::operator==(const Transaction& rhs) const
{
    return this->hash() == rhs.hash();
}

void bcostars::protocol::TransactionImpl::decode(bcos::bytesConstRef _txData)
{
    bcos::concepts::serialize::decode(_txData, *m_inner());
}

void bcostars::protocol::TransactionImpl::encode(bcos::bytes& txData) const
{
    bcos::concepts::serialize::encode(*m_inner(), txData);
}

bcos::crypto::HashType bcostars::protocol::TransactionImpl::hash() const
{
    if (m_inner()->dataHash.empty() && m_inner()->extraTransactionHash.empty())
    {
        throwTrace(EmptyTransactionHash{});
    }

    if (type() == static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        bcos::crypto::HashType hashResult((bcos::byte*)m_inner()->extraTransactionHash.data(),
            m_inner()->extraTransactionHash.size());
        return hashResult;
    }
    bcos::crypto::HashType hashResult(
        (bcos::byte*)m_inner()->dataHash.data(), m_inner()->dataHash.size());

    return hashResult;
}

void bcostars::protocol::TransactionImpl::calculateHash(const bcos::crypto::Hash& hashImpl)
{
    bcos::concepts::hash::calculate(*m_inner(), hashImpl.hasher(), m_inner()->dataHash);
}

std::string_view bcostars::protocol::TransactionImpl::nonce() const
{
    return m_inner()->data.nonce;
}

bcos::bytesConstRef bcostars::protocol::TransactionImpl::input() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->data.input.data()),
        m_inner()->data.input.size()};
}
int32_t bcostars::protocol::TransactionImpl::version() const
{
    return m_inner()->data.version;
}
std::string_view bcostars::protocol::TransactionImpl::chainId() const
{
    return m_inner()->data.chainID;
}
std::string_view bcostars::protocol::TransactionImpl::groupId() const
{
    return m_inner()->data.groupID;
}
int64_t bcostars::protocol::TransactionImpl::blockLimit() const
{
    return m_inner()->data.blockLimit;
}
void bcostars::protocol::TransactionImpl::setNonce(std::string nonce)
{
    m_inner()->data.nonce = std::move(nonce);
}
std::string_view bcostars::protocol::TransactionImpl::to() const
{
    return m_inner()->data.to;
}
std::string_view bcostars::protocol::TransactionImpl::abi() const
{
    return m_inner()->data.abi;
}

std::string_view bcostars::protocol::TransactionImpl::value() const
{
    return m_inner()->data.value;
}

std::string_view bcostars::protocol::TransactionImpl::gasPrice() const
{
    return m_inner()->data.gasPrice;
}

int64_t bcostars::protocol::TransactionImpl::gasLimit() const
{
    return m_inner()->data.gasLimit;
}

std::string_view bcostars::protocol::TransactionImpl::maxFeePerGas() const
{
    return m_inner()->data.maxFeePerGas;
}

std::string_view bcostars::protocol::TransactionImpl::maxPriorityFeePerGas() const
{
    return m_inner()->data.maxPriorityFeePerGas;
}

bcos::bytesConstRef bcostars::protocol::TransactionImpl::extension() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->data.extension.data()),
        m_inner()->data.extension.size()};
}

int64_t bcostars::protocol::TransactionImpl::importTime() const
{
    return m_inner()->importTime;
}
void bcostars::protocol::TransactionImpl::setImportTime(int64_t _importTime)
{
    m_inner()->importTime = _importTime;
}
bcos::bytesConstRef bcostars::protocol::TransactionImpl::signatureData() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->signature.data()),
        m_inner()->signature.size()};
}
std::string_view bcostars::protocol::TransactionImpl::sender() const
{
    return {m_inner()->sender.data(), m_inner()->sender.size()};
}
void bcostars::protocol::TransactionImpl::forceSender(const bcos::bytes& _sender)
{
    if (!tainted())
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("sender of clean transaction is immutable"));
    }
    m_inner()->sender.assign(_sender.begin(), _sender.end());
}
void bcostars::protocol::TransactionImpl::clearSenderAndHash()
{
    m_inner()->sender.clear();
    m_inner()->dataHash.clear();
    // FIB-New1: discard wire-supplied canonical txHash so verify() recomputes it
    m_inner()->extraTransactionHash.clear();
    setTainted(true);
}

void bcostars::protocol::TransactionImpl::verify(
    bcos::crypto::Hash& hashImpl, bcos::crypto::SignatureCrypto& signatureImpl)
{
    // BCOS branch: delegate to base-class behaviour (no change)
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        bcos::protocol::Transaction::verify(hashImpl, signatureImpl);
        return;
    }
    // Web3 branch
    if (!tainted())
    {
        return;
    }

    auto const payloadBytes = extraTransactionBytes();
    auto const signingHash = bcos::crypto::keccak256Hash(payloadBytes);

    auto const signature = signatureData();
    auto [recovered, sender] = signatureImpl.recoverAddress(hashImpl, signingHash, signature);
    if (!recovered) [[unlikely]]
    {
        BCOS_LOG(INFO) << LOG_DESC("recover sender address failed (Web3)")
                       << LOG_KV("hash", signingHash.abridged());
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("recover sender address from Web3 signature failed"));
    }

    // Canonical txHash recompute: only when the wire-supplied value was cleared
    // (e.g. by clearSenderAndHash in P2P import path). RPC pre-write is honoured.
    if (m_inner()->extraTransactionHash.empty())
    {
        if (signature.size() != 65) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("invalid Web3 signature length, expect 65"));
        }
        bcos::rpc::Web3Transaction web3Tx;
        // Construct a non-const bytesRef view into payloadBytes (decode mutates the view,
        // not the underlying bytes).
        bcos::bytesRef in(const_cast<bcos::byte*>(payloadBytes.data()), payloadBytes.size());
        if (auto err = bcos::codec::rlp::decodeFromPayload(in, web3Tx); err) [[unlikely]]
        {
            BCOS_LOG(INFO) << LOG_DESC("decode Web3Transaction payload for canonical hash failed")
                           << LOG_KV("error", err->errorMessage());
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("decode Web3Transaction payload for canonical hash failed: " +
                                      err->errorMessage()));
        }
        // Inject signature parts. Signature wire format is r(32) || s(32) || v(1).
        web3Tx.signatureR.assign(signature.begin(), signature.begin() + 32);
        web3Tx.signatureS.assign(signature.begin() + 32, signature.begin() + 64);
        web3Tx.signatureV = static_cast<uint64_t>(signature[64]);
        // For Legacy+EIP-155, decodeFromPayload does not surface chainId; populate from
        // the tars data.chainID field (the trusted, locally-computed canonical source).
        if (!web3Tx.chainId.has_value())
        {
            auto const& chainIdStr = m_inner()->data.chainID;
            if (!chainIdStr.empty())
            {
                uint64_t cid = 0;
                auto const* first = chainIdStr.data();
                auto const* last = first + chainIdStr.size();
                auto const result = std::from_chars(first, last, cid);
                if (result.ec == std::errc{} && cid != 0)
                {
                    web3Tx.chainId.emplace(cid);
                }
            }
        }
        auto const canonicalTxHash = web3Tx.txHash();
        m_inner()->extraTransactionHash.assign(canonicalTxHash.begin(), canonicalTxHash.end());
    }

    // dataHash field is the signing-hash for Web3; populate when missing so the
    // tars-schema invariant ("dataHash always populated post-verify") holds.
    if (m_inner()->dataHash.empty())
    {
        m_inner()->dataHash.assign(signingHash.begin(), signingHash.end());
    }

    forceSender(sender);
    setTainted(false);
}
void bcostars::protocol::TransactionImpl::setSignatureData(bcos::bytes& signature)
{
    m_inner()->signature.assign(signature.begin(), signature.end());
}
int32_t bcostars::protocol::TransactionImpl::attribute() const
{
    return m_inner()->attribute;
}
void bcostars::protocol::TransactionImpl::setAttribute(int32_t attribute)
{
    m_inner()->attribute |= attribute;
}
std::string_view bcostars::protocol::TransactionImpl::extraData() const
{
    return m_inner()->extraData;
}
uint8_t bcostars::protocol::TransactionImpl::type() const
{
    return static_cast<uint8_t>(m_inner()->type);
}
bcos::bytesConstRef bcostars::protocol::TransactionImpl::extraTransactionBytes() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->extraTransactionBytes.data()),
        m_inner()->extraTransactionBytes.size()};
}

const bcostars::Transaction& bcostars::protocol::TransactionImpl::inner() const
{
    return *m_inner();
}
bcostars::Transaction& bcostars::protocol::TransactionImpl::mutableInner()
{
    return *m_inner();
}
void bcostars::protocol::TransactionImpl::setInner(bcostars::Transaction inner)
{
    *m_inner() = std::move(inner);
}

size_t bcostars::protocol::TransactionImpl::size() const
{
    size_t size = 0;
    size += m_inner()->data.nonce.size();
    size += m_inner()->data.to.size();
    size += m_inner()->data.input.size();
    size += m_inner()->data.abi.size();
    size += m_inner()->data.value.size();
    size += m_inner()->data.gasPrice.size();
    size += m_inner()->data.maxFeePerGas.size();
    size += m_inner()->data.maxPriorityFeePerGas.size();
    size += m_inner()->data.extension.size();
    size += m_inner()->signature.size();
    size += m_inner()->sender.size();
    size += m_inner()->extraData.size();
    size += m_inner()->extraTransactionBytes.size();
    size += m_inner()->extraTransactionHash.size();
    return size;
}
