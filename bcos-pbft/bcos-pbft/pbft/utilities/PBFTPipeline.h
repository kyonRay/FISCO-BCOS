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
 * @brief 3-stage admission pipeline for PBFT messages before m_msgQueue push.
 *        Fixes FIB-145 (unbounded m_msgQueue during sync → OOM) and
 *        FIB-146 (re-queued future packets → CPU busy-spin + OOM).
 *
 * Pipeline stages:
 *   Stage 1 – Stale-height filter: drop messages whose index() < lastApplied.
 *             Commit and CheckPoint packets are NEVER dropped to preserve safety.
 *   Stage 2 – LRU per-peer dedup: drop messages already recently seen from
 *             the same peer (keyed by packetType + index + hash).
 *             Per-peer bounded LRU; evicts oldest entry on capacity overflow.
 *   Stage 3 – Per-type admission capacity:
 *             PrePrepare and Prepare: bounded queue size per-peer; on overflow set
 *             backpressure flag for that peer, suppress further PrePrepare/Prepare
 *             from that peer (Commit/CheckPoint from the same peer still admitted).
 *             Commit and CheckPoint: always admitted, never suppressed.
 *
 * @file PBFTPipeline.h
 * @author: claude
 * @date 2026-05-07
 */
#pragma once
#include "bcos-pbft/pbft/interfaces/PBFTBaseMessageInterface.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/BoostLog.h>
#include <oneapi/tbb/concurrent_unordered_map.h>
#include <atomic>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace bcos::consensus
{

// ─── Stage 2 LRU per-peer cache ─────────────────────────────────────────────
// FIB-146: real per-peer LRU dedup cache (replaces E.6a passthrough stub).
//
// Keys are strings of the form "<packetType>:<blockIndex>:<hashHex>".
// When capacity is reached, the least-recently-used entry is evicted to make
// room. This bounds per-peer memory while still detecting near-term duplicates.
//
// Thread-safety: NOT thread-safe. The caller (PBFTPipeline) holds m_lruMutex
// when calling seenAndInsert(), so no internal locking is needed.
class PeerLRUCache
{
public:
    explicit PeerLRUCache(size_t capacity) : m_capacity(capacity == 0 ? 1 : capacity) {}

    // Returns true if the key has been seen recently (should be dropped).
    // Inserts the key into the cache (evicting the LRU entry if full).
    bool seenAndInsert(std::string const& key)
    {
        auto it = m_map.find(key);
        if (it != m_map.end())
        {
            // Cache hit: move to front (most recently used)
            m_list.splice(m_list.begin(), m_list, it->second);
            return true;
        }

        // Cache miss: evict LRU entry if at capacity
        if (m_list.size() >= m_capacity)
        {
            m_map.erase(m_list.back());
            m_list.pop_back();
        }

        // Insert new key at front
        m_list.push_front(key);
        m_map.emplace(key, m_list.begin());
        return false;
    }

private:
    size_t m_capacity;
    std::list<std::string> m_list;  // front = most recently used
    std::unordered_map<std::string, std::list<std::string>::iterator> m_map;
};

// ─── Admission pipeline ───────────────────────────────────────────────────────
class PBFTPipeline
{
public:
    using MessagePtr = std::shared_ptr<PBFTBaseMessageInterface>;
    using PeerID = std::string;  // hex string of peer node-id

    // Packet types that are safety-critical and must never be dropped
    static bool isSafetyCritical(PacketType type)
    {
        return type == PacketType::CommitPacket || type == PacketType::CheckPoint;
    }

    struct Config
    {
        // Stage 3: max pending PrePrepare+Prepare messages per peer before backpressure
        size_t perPeerCapacity{64};

        // Stage 2: LRU cache capacity (per peer, currently stub)
        size_t lruCapacity{256};
    };

    explicit PBFTPipeline(Config cfg) : m_cfg(std::move(cfg)) {}
    PBFTPipeline() : m_cfg{} {}

    // Return true if the message should be admitted to m_msgQueue.
    // Takes the lastApplied block number for Stage 1.
    bool admit(MessagePtr const& msg, bcos::protocol::BlockNumber lastApplied)
    {
        auto peerKey = peerIDKey(msg);
        auto type = msg->packetType();

        // ── Stage 1: stale-height filter ────────────────────────────────────
        // Drop messages whose height is strictly below lastApplied,
        // UNLESS they are safety-critical (Commit/CheckPoint).
        if (!isSafetyCritical(type) && msg->index() < lastApplied)
        {
            PBFT_LOG(TRACE) << LOG_DESC("[PBFTPipeline] Stage1: drop stale msg")
                            << LOG_KV("type", type) << LOG_KV("index", msg->index())
                            << LOG_KV("lastApplied", lastApplied);
            return false;
        }

        // ── Stage 2: LRU per-peer dedup (FIB-146) ───────────────────────────
        // Drop messages whose (type:index:hash) tuple was recently seen from
        // this peer. The LRU cache evicts the oldest entry when full, bounding
        // per-peer memory while still catching near-term re-queued duplicates.
        {
            auto& cache = getOrCreateCache(peerKey);
            std::string dedupKey =
                std::to_string(type) + ":" + std::to_string(msg->index()) + ":" + msg->hash().hex();
            if (cache.seenAndInsert(dedupKey))
            {
                PBFT_LOG(TRACE) << LOG_DESC("[PBFTPipeline] Stage2: drop dedup msg")
                                << LOG_KV("type", type) << LOG_KV("index", msg->index());
                return false;
            }
        }

        // ── Stage 3: per-type admission capacity ─────────────────────────────
        // Commit/CheckPoint: always admitted.
        if (isSafetyCritical(type))
        {
            return true;
        }

        // PrePrepare/Prepare and others: check per-peer backpressure and capacity.
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            // If peer is under backpressure, suppress PrePrepare/Prepare
            if (m_backpressuredPeers.count(peerKey))
            {
                PBFT_LOG(TRACE) << LOG_DESC("[PBFTPipeline] Stage3: drop backpressured peer msg")
                                << LOG_KV("type", type) << LOG_KV("index", msg->index());
                return false;
            }

            auto& count = m_peerPendingCount[peerKey];
            if (count >= m_cfg.perPeerCapacity)
            {
                // Set backpressure for this peer
                m_backpressuredPeers.insert(peerKey);
                PBFT_LOG(INFO) << LOG_DESC(
                                      "[PBFTPipeline] Stage3: peer capacity exceeded, "
                                      "setting backpressure")
                               << LOG_KV("peer", peerKey)
                               << LOG_KV("capacity", m_cfg.perPeerCapacity) << LOG_KV("type", type);
                return false;
            }
            ++count;
        }
        return true;
    }

    // Call when a message is consumed from m_msgQueue to decrement the counter.
    void consumed(MessagePtr const& msg)
    {
        auto peerKey = peerIDKey(msg);
        auto type = msg->packetType();
        if (isSafetyCritical(type))
        {
            return;  // safety-critical messages weren't counted
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_peerPendingCount.find(peerKey);
        if (it != m_peerPendingCount.end() && it->second > 0)
        {
            --it->second;
            if (it->second < m_cfg.perPeerCapacity / 2)
            {
                // Clear backpressure when queue drains to half capacity
                m_backpressuredPeers.erase(peerKey);
            }
        }
    }

    // Clear backpressure for all peers (e.g. after view change)
    void clearAllBackpressure()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backpressuredPeers.clear();
        m_peerPendingCount.clear();
    }

    bool isPeerBackpressured(PeerID const& peer) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_backpressuredPeers.count(peer) > 0;
    }

    size_t peerPendingCount(PeerID const& peer) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_peerPendingCount.find(peer);
        return it != m_peerPendingCount.end() ? it->second : 0U;
    }

private:
    static PeerID peerIDKey(MessagePtr const& msg)
    {
        auto from = msg->from();
        return from ? from->hex() : "unknown";
    }

    PeerLRUCache& getOrCreateCache(PeerID const& peer)
    {
        std::lock_guard<std::mutex> lock(m_lruMutex);
        auto it = m_lruCaches.find(peer);
        if (it == m_lruCaches.end())
        {
            m_lruCaches.emplace(peer, PeerLRUCache{m_cfg.lruCapacity});
            return m_lruCaches.at(peer);
        }
        return it->second;
    }

    Config m_cfg;

    mutable std::mutex m_mutex;
    std::unordered_map<PeerID, size_t> m_peerPendingCount;
    std::unordered_set<PeerID> m_backpressuredPeers;

    std::mutex m_lruMutex;
    std::unordered_map<PeerID, PeerLRUCache> m_lruCaches;
};

}  // namespace bcos::consensus
