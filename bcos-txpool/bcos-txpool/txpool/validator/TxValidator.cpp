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
 * @brief implementation of TxValidator
 * @file TxValidator.cpp
 * @author: yujiechen
 * @date 2021-05-11
 */
#include "TxValidator.h"

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

TxCheckResult TxValidator::verify(bcos::protocol::Transaction::ConstPtr _tx)
{
    if (_tx->invalid()) [[unlikely]]
    {
        return {TransactionStatus::InvalidSignature, std::monostate{}};
    }
    // check groupId and chainId
    if (_tx->groupId() != m_groupId) [[unlikely]]
    {
        return {TransactionStatus::InvalidGroupId, std::monostate{}};
    }
    if (_tx->chainId() != m_chainId) [[unlikely]]
    {
        return {TransactionStatus::InvalidChainId, std::monostate{}};
    }
    // compare with nonces cached in memory, only check nonce in txpool
    auto [status, value] = checkTxpoolNonce(_tx);
    if (status != TransactionStatus::None)
    {
        return {status, std::move(value)};
    }
    // check ledger nonce and block limit
    std::tie(status, value) = checkLedgerNonceAndBlockLimit(_tx);
    if (status != TransactionStatus::None)
    {
        return {status, std::move(value)};
    }
    // check signature
    try
    {
        _tx->verify(*m_cryptoSuite->hashImpl(), *m_cryptoSuite->signatureImpl());
    }
    catch (...)
    {
        return {TransactionStatus::InvalidSignature, std::monostate{}};
    }

    if (isSystemTransaction(_tx))
    {
        _tx->setSystemTx(true);
    }
    m_txPoolNonceChecker->insert(_tx->nonce(), _tx->hash());
    return {TransactionStatus::None, std::monostate{}};
}

TxCheckResult TxValidator::checkLedgerNonceAndBlockLimit(bcos::protocol::Transaction::ConstPtr _tx)
{
    // compare with nonces stored on-chain, and check block limit inside
    auto&& [status, value] = m_ledgerNonceChecker->checkNonce(_tx);
    if (status != TransactionStatus::None)
    {
        return {status, std::move(value)};
    }
    if (isSystemTransaction(_tx))
    {
        _tx->setSystemTx(true);
    }
    return {TransactionStatus::None, std::monostate{}};
}

TxCheckResult TxValidator::checkTxpoolNonce(bcos::protocol::Transaction::ConstPtr _tx)
{
    return m_txPoolNonceChecker->checkNonce(_tx, false);
}
