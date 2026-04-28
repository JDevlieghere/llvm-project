//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_LRUCACHE_H
#define LLDB_UTILITY_LRUCACHE_H

#include "llvm/ADT/DenseMap.h"

#include <cassert>
#include <chrono>
#include <list>
#include <optional>

namespace lldb_private {

/// A generic LRU cache with optional time-to-live (TTL) support.
///
/// Provides O(1) lookup, insertion, and LRU update using a combination of
/// llvm::DenseMap and std::list. When the cache exceeds its maximum capacity,
/// the least-recently-used entry is evicted. Entries older than the TTL are
/// treated as missing on lookup and lazily evicted.
///
/// This class is not thread-safe. Callers must provide their own
/// synchronization if the cache is accessed from multiple threads.
template <typename KeyT, typename ValueT,
          typename ClockT = std::chrono::steady_clock>
class LRUCache {
public:
  using Duration = typename ClockT::duration;
  using TimePoint = typename ClockT::time_point;

  /// Construct an LRU cache.
  ///
  /// \param max_size Maximum number of entries. 0 means unlimited (no
  ///                 capacity-based eviction).
  /// \param ttl      Time-to-live for entries. Duration::zero() means entries
  ///                 never expire based on time.
  explicit LRUCache(size_t max_size, Duration ttl = Duration::zero())
      : m_max_size(max_size), m_ttl(ttl) {}

  /// Insert or update a key-value pair.
  ///
  /// If the key already exists, its value is updated and it is promoted to
  /// the most-recently-used position. If inserting a new entry would exceed
  /// capacity, the least-recently-used entry is evicted first.
  void Set(const KeyT &key, ValueT value) {
    auto now = ClockT::now();
    auto it = m_lookup.find(key);
    if (it != m_lookup.end()) {
      it->second->m_value = std::move(value);
      it->second->m_timestamp = now;
      m_items.splice(m_items.begin(), m_items, it->second);
      return;
    }
    if (m_max_size > 0 && m_items.size() >= m_max_size)
      EvictLRU();
    m_items.push_front({key, std::move(value), now});
    m_lookup.insert({key, m_items.begin()});
  }

  /// Look up a key.
  ///
  /// Returns the value if found and not expired, or std::nullopt otherwise.
  /// A successful lookup promotes the entry to the most-recently-used position
  /// and refreshes its TTL timestamp.
  std::optional<ValueT> Get(const KeyT &key) {
    auto it = m_lookup.find(key);
    if (it == m_lookup.end())
      return std::nullopt;
    auto now = ClockT::now();
    if (m_ttl > Duration::zero() && (now - it->second->m_timestamp) > m_ttl) {
      m_items.erase(it->second);
      m_lookup.erase(it);
      return std::nullopt;
    }
    it->second->m_timestamp = now;
    m_items.splice(m_items.begin(), m_items, it->second);
    return it->second->m_value;
  }

  /// Erase a single entry by key.
  ///
  /// \return True if the key was found and removed, false otherwise.
  bool Erase(const KeyT &key) {
    auto it = m_lookup.find(key);
    if (it == m_lookup.end())
      return false;
    m_items.erase(it->second);
    m_lookup.erase(it);
    return true;
  }

  /// Remove all entries.
  void Clear() {
    m_items.clear();
    m_lookup.clear();
  }

  /// Return true if the cache contains no entries.
  bool Empty() const { return m_items.empty(); }

  /// Return the current number of entries.
  ///
  /// \note This may include entries that have expired but have not yet been
  /// lazily evicted via Get().
  size_t Size() const { return m_items.size(); }

  /// Return the maximum capacity. 0 means unlimited.
  size_t MaxSize() const { return m_max_size; }

private:
  struct CacheEntry {
    KeyT m_key;
    ValueT m_value;
    TimePoint m_timestamp;
  };

  using ListType = std::list<CacheEntry>;
  using ListIterator = typename ListType::iterator;

  void EvictLRU() {
    assert(!m_items.empty() && "Cannot evict from empty cache");
    m_lookup.erase(m_items.back().m_key);
    m_items.pop_back();
  }

  ListType m_items; // Front = MRU, back = LRU.
  llvm::DenseMap<KeyT, ListIterator> m_lookup;
  size_t m_max_size;
  Duration m_ttl;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_LRUCACHE_H
