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
 * @brief interface for nonce check
 * @file NonceCheckerInterface.h
 * @author: yujiechen
 * @date 2021-05-08
 */
#pragma once
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-protocol/TransactionStatus.h>
#include <tbb/concurrent_unordered_set.h>

#define NONCECHECKER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TXPOOL") << LOG_BADGE("NonceChecker")

namespace bcos::txpool
{
class NonceCheckerInterface
{
public:
    using Ptr = std::shared_ptr<NonceCheckerInterface>;
    NonceCheckerInterface() = default;
    virtual ~NonceCheckerInterface() = default;

    virtual bcos::protocol::TransactionStatus checkNonce(
        const bcos::protocol::Transaction& _tx) = 0;
    virtual bool exists(bcos::protocol::NonceType const& _nonce) = 0;
    virtual void batchInsert(
        bcos::protocol::BlockNumber _batchId, bcos::protocol::NonceListPtr const& _nonceList) = 0;
    virtual void batchRemove(bcos::protocol::NonceList const& _nonceList) = 0;
    virtual void batchRemove(tbb::concurrent_unordered_set<bcos::protocol::NonceType,
        std::hash<bcos::protocol::NonceType>> const& _nonceList) = 0;
    virtual void insert(bcos::protocol::NonceType const& _nonce) = 0;

    virtual void remove(bcos::protocol::NonceType const& _nonce) = 0;
};
}  // namespace bcos::txpool
