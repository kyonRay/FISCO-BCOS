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
 * @brief an implementation of using memory to store transactions
 * @file MemoryStorage.h
 * @author: yujiechen
 * @date 2021-05-07
 */
#pragma once

#include "bcos-task/Task.h"
#include "bcos-txpool/TxPoolConfig.h"
#include "bcos-txpool/txpool/utilities/Common.h"
#include "txpool/interfaces/TxPoolStorageInterface.h"
#include <bcos-utilities/BucketMap.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/Timer.h>
#include <tbb/concurrent_queue.h>
#include <boost/thread/pthread/shared_mutex.hpp>

namespace bcos::txpool
{

class MemoryStorage : public TxPoolStorageInterface,
                      public std::enable_shared_from_this<MemoryStorage>
{
public:
    // the default txsExpirationTime is 10 minutes
    explicit MemoryStorage(TxPoolConfig::Ptr _config, size_t _notifyWorkerNum = 2,
        uint64_t _txsExpirationTime = TX_DEFAULT_EXPIRATION_TIME);
    ~MemoryStorage() override { stop(); };

    // New interfaces =============
    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        protocol::Transaction::Ptr transaction) override;

    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransactionWithoutReceipt(
        protocol::Transaction::Ptr transaction) override;

    std::vector<protocol::Transaction::ConstPtr> getTransactions(
        ::ranges::any_view<bcos::h256, ::ranges::category::mask | ::ranges::category::sized> hashes)
        override;
    // ============================

    // disassemble submitTransaction
    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransactionWithHook(
        protocol::Transaction::Ptr transaction,
        std::function<void()> afterInsertHook = nullptr) override;

    bcos::protocol::TransactionStatus insert(bcos::protocol::Transaction::Ptr transaction) override;

    [[deprecated(
        "do not use raw insert tx pool without check, use batchVerifyAndSubmitTransaction")]] void
    batchInsert(bcos::protocol::Transactions const& _txs) override;

    bcos::protocol::Transaction::Ptr remove(bcos::crypto::HashType const& _txHash) override;
    // invoke when scheduler finished block executed and notify txpool new block result
    void batchRemove(bcos::protocol::BlockNumber _batchId,
        bcos::protocol::TransactionSubmitResults const& _txsResult) override;
    bcos::protocol::Transaction::Ptr removeSubmittedTx(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult) override;

    bcos::protocol::ConstTransactionsPtr fetchTxs(
        bcos::crypto::HashList& _missedTxs, bcos::crypto::HashList const& _txsList) override;

    // FIXME: deprecated, after using txpool::broadcastTransaction
    bcos::protocol::ConstTransactionsPtr fetchNewTxs(size_t _txsLimit) override;

    bool batchFetchTxs(bcos::protocol::Block::Ptr _txsList, bcos::protocol::Block::Ptr _sysTxsList,
        size_t _txsLimit, TxsHashSetPtr _avoidTxs, bool _avoidDuplicate = true) override;

    bool exist(bcos::crypto::HashType const& _txHash) override
    {
        TxsMap::ReadAccessor accessor;
        return m_txsTable.find<TxsMap::ReadAccessor>(accessor, _txHash);
    }
    size_t size() const override { return m_txsTable.size(); }
    void clear() override;

    // FIXME: deprecated, after using txpool::broadcastTransaction
    bcos::crypto::HashListPtr filterUnknownTxs(
        bcos::crypto::HashList const& _txsHashList, bcos::crypto::NodeIDPtr _peer) override;

    bcos::crypto::HashListPtr getTxsHash(int _limit) override;
    void batchMarkAllTxs(bool _sealFlag) override;

    void stop() override;
    void start() override;

    std::shared_ptr<bcos::crypto::HashList> batchVerifyProposal(
        bcos::protocol::Block::Ptr _block) override;

    bool batchVerifyProposal(std::shared_ptr<bcos::crypto::HashList> _txsHashList) override;

    bool batchVerifyAndSubmitTransaction(
        bcos::protocol::BlockHeader::Ptr _header, bcos::protocol::TransactionsPtr _txs) override;
    void batchImportTxs(bcos::protocol::TransactionsPtr _txs) override;

    // return true if all txs have been marked
    bool batchMarkTxs(bcos::crypto::HashList const& _txsHashList,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        bool _sealFlag) override;

    bcos::protocol::TransactionStatus verifyAndSubmitTransaction(
        protocol::Transaction::Ptr transaction, protocol::TxSubmitCallback txSubmitCallback,
        bool checkPoolLimit, bool lock);


protected:
    virtual void notifyTxsSize(size_t _retryTime = 0);

    bcos::protocol::TransactionStatus insertWithoutLock(
        bcos::protocol::Transaction::Ptr transaction);
    bcos::protocol::TransactionStatus enforceSubmitTransaction(
        bcos::protocol::Transaction::Ptr _tx);
    bcos::protocol::TransactionStatus txpoolStorageCheck(
        const bcos::protocol::Transaction& transaction,
        protocol::TxSubmitCallback& txSubmitCallback);

    void onTxRemoved(const bcos::protocol::Transaction::Ptr& _tx, bool needNotifyUnsealedTxsSize);

    virtual bcos::protocol::Transaction::Ptr removeWithoutNotifyUnseal(
        bcos::crypto::HashType const& _txHash);
    virtual bcos::protocol::Transaction::Ptr removeSubmittedTxWithoutLock(
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult, bool _notify = true);

    virtual void notifyInvalidReceipt(bcos::crypto::HashType const& _txHash,
        bcos::protocol::TransactionStatus _status,
        bcos::protocol::TxSubmitCallback _txSubmitCallback);

    virtual void notifyTxResult(bcos::protocol::Transaction& transaction,
        bcos::protocol::TransactionSubmitResult::Ptr txSubmitResult);

    virtual void removeInvalidTxs(bool lock);
    virtual void cleanUpExpiredTransactions();

    // return true if all txs have been marked
    virtual bool batchMarkTxsWithoutLock(bcos::crypto::HashList const& _txsHashList,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        bool _sealFlag);

    void printPendingTxs() override;

    TxPoolConfig::Ptr m_config;

    using TxsMap = BucketMap<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr>;
    TxsMap m_txsTable;
    TxsMap m_invalidTxs;

    using HashSet = BucketSet<bcos::crypto::HashType, std::hash<bcos::crypto::HashType>>;
    HashSet m_missedTxs;

    std::atomic<bcos::protocol::BlockNumber> m_blockNumber = {0};
    uint64_t m_blockNumberUpdatedTime;

    // the txs expiration time, default is 10 minutes
    uint64_t m_txsExpirationTime = TX_DEFAULT_EXPIRATION_TIME;
    // timer to clear up the expired txs in-period
    std::shared_ptr<Timer> m_cleanUpTimer;
    // timer to notify txs size
    std::shared_ptr<Timer> m_txsSizeNotifierTimer;

    // for tps stat
    std::atomic_uint64_t m_tpsStatstartTime = {0};
    std::atomic_uint64_t m_onChainTxsCount = {0};
    bcos::crypto::HashType m_knownLatestSealedTxHash;
};
}  // namespace bcos::txpool