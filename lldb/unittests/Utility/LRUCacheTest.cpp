//===-- LRUCacheTest.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/LRUCache.h"
#include "gtest/gtest.h"
#include <memory>

using namespace lldb_private;

namespace {

struct MockClock {
  using duration = std::chrono::steady_clock::duration;
  using time_point = std::chrono::steady_clock::time_point;
  using rep = duration::rep;
  using period = duration::period;

  static time_point Now;
  static time_point now() { return Now; }

  static void Advance(duration d) { Now += d; }
  static void Reset() { Now = time_point(duration::zero()); }
};

MockClock::time_point MockClock::Now =
    MockClock::time_point(MockClock::duration::zero());

using TestCache = LRUCache<int, std::string, MockClock>;

class LRUCacheTest : public ::testing::Test {
protected:
  void SetUp() override { MockClock::Reset(); }
};

} // namespace

TEST_F(LRUCacheTest, BasicSetAndGet) {
  TestCache cache(4);
  cache.Set(1, "one");
  auto val = cache.Get(1);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "one");
}

TEST_F(LRUCacheTest, GetMissReturnsNullopt) {
  TestCache cache(4);
  EXPECT_EQ(cache.Get(42), std::nullopt);
}

TEST_F(LRUCacheTest, SetOverwritesExistingKey) {
  TestCache cache(4);
  cache.Set(1, "one");
  cache.Set(1, "ONE");
  auto val = cache.Get(1);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "ONE");
  EXPECT_EQ(cache.Size(), 1u);
}

TEST_F(LRUCacheTest, CapacityEviction) {
  TestCache cache(2);
  cache.Set(1, "one");
  cache.Set(2, "two");
  cache.Set(3, "three");
  EXPECT_EQ(cache.Get(1), std::nullopt);
  EXPECT_EQ(cache.Get(2), std::string("two"));
  EXPECT_EQ(cache.Get(3), std::string("three"));
  EXPECT_EQ(cache.Size(), 2u);
}

TEST_F(LRUCacheTest, LRUOrderRefreshedOnGet) {
  TestCache cache(3);
  cache.Set(1, "one");
  cache.Set(2, "two");
  cache.Set(3, "three");
  // Access key 1 to promote it.
  cache.Get(1);
  // Insert key 4 — should evict key 2 (the LRU).
  cache.Set(4, "four");
  EXPECT_EQ(cache.Get(2), std::nullopt);
  EXPECT_NE(cache.Get(1), std::nullopt);
  EXPECT_NE(cache.Get(3), std::nullopt);
  EXPECT_NE(cache.Get(4), std::nullopt);
}

TEST_F(LRUCacheTest, LRUOrderRefreshedOnSet) {
  TestCache cache(2);
  cache.Set(1, "one");
  cache.Set(2, "two");
  // Update key 1 to promote it.
  cache.Set(1, "ONE");
  // Insert key 3 — should evict key 2 (the LRU).
  cache.Set(3, "three");
  EXPECT_EQ(cache.Get(2), std::nullopt);
  EXPECT_EQ(cache.Get(1), std::string("ONE"));
  EXPECT_NE(cache.Get(3), std::nullopt);
}

TEST_F(LRUCacheTest, TTLExpiration) {
  using namespace std::chrono_literals;
  TestCache cache(4, 100ms);
  cache.Set(1, "one");
  MockClock::Advance(200ms);
  EXPECT_EQ(cache.Get(1), std::nullopt);
  EXPECT_EQ(cache.Size(), 0u);
}

TEST_F(LRUCacheTest, TTLRefreshOnGet) {
  using namespace std::chrono_literals;
  TestCache cache(4, 100ms);
  cache.Set(1, "one");
  MockClock::Advance(80ms);
  // Access before expiry — refreshes timestamp.
  auto val = cache.Get(1);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "one");
  // Advance another 80ms (total 160ms from insertion, but only 80ms from
  // last access). Entry should still be alive.
  MockClock::Advance(80ms);
  val = cache.Get(1);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "one");
  // Advance past TTL from last access.
  MockClock::Advance(200ms);
  EXPECT_EQ(cache.Get(1), std::nullopt);
}

TEST_F(LRUCacheTest, TTLRefreshOnSet) {
  using namespace std::chrono_literals;
  TestCache cache(4, 100ms);
  cache.Set(1, "one");
  MockClock::Advance(80ms);
  // Update before expiry — refreshes timestamp.
  cache.Set(1, "ONE");
  MockClock::Advance(80ms);
  // 80ms from last Set — should still be alive.
  auto val = cache.Get(1);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "ONE");
}

TEST_F(LRUCacheTest, EraseExistingKey) {
  TestCache cache(4);
  cache.Set(1, "one");
  cache.Set(2, "two");
  EXPECT_TRUE(cache.Erase(1));
  EXPECT_EQ(cache.Get(1), std::nullopt);
  EXPECT_EQ(cache.Size(), 1u);
  EXPECT_NE(cache.Get(2), std::nullopt);
}

TEST_F(LRUCacheTest, EraseNonExistentKey) {
  TestCache cache(4);
  EXPECT_FALSE(cache.Erase(42));
}

TEST_F(LRUCacheTest, Clear) {
  TestCache cache(4);
  cache.Set(1, "one");
  cache.Set(2, "two");
  cache.Set(3, "three");
  cache.Clear();
  EXPECT_EQ(cache.Size(), 0u);
  EXPECT_EQ(cache.Get(1), std::nullopt);
  EXPECT_EQ(cache.Get(2), std::nullopt);
  EXPECT_EQ(cache.Get(3), std::nullopt);
}

TEST_F(LRUCacheTest, CapacityOne) {
  TestCache cache(1);
  cache.Set(1, "one");
  EXPECT_NE(cache.Get(1), std::nullopt);
  cache.Set(2, "two");
  EXPECT_EQ(cache.Get(1), std::nullopt);
  EXPECT_NE(cache.Get(2), std::nullopt);
  EXPECT_EQ(cache.Size(), 1u);
}

TEST_F(LRUCacheTest, UnlimitedCapacity) {
  TestCache cache(0);
  for (int i = 0; i < 1000; ++i)
    cache.Set(i, std::to_string(i));
  EXPECT_EQ(cache.Size(), 1000u);
  for (int i = 0; i < 1000; ++i)
    EXPECT_NE(cache.Get(i), std::nullopt);
}

TEST_F(LRUCacheTest, SizeTracking) {
  TestCache cache(4);
  EXPECT_EQ(cache.Size(), 0u);
  cache.Set(1, "one");
  EXPECT_EQ(cache.Size(), 1u);
  cache.Set(2, "two");
  EXPECT_EQ(cache.Size(), 2u);
  // Overwrite — size unchanged.
  cache.Set(1, "ONE");
  EXPECT_EQ(cache.Size(), 2u);
  cache.Erase(2);
  EXPECT_EQ(cache.Size(), 1u);
  cache.Clear();
  EXPECT_EQ(cache.Size(), 0u);
}

TEST_F(LRUCacheTest, SharedPtrValue) {
  LRUCache<int, std::shared_ptr<int>, MockClock> cache(2);
  cache.Set(1, std::make_shared<int>(42));
  auto val = cache.Get(1);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(**val, 42);
  cache.Set(2, std::make_shared<int>(99));
  cache.Set(3, std::make_shared<int>(7));
  // Key 1 should be evicted.
  EXPECT_EQ(cache.Get(1), std::nullopt);
  auto val3 = cache.Get(3);
  ASSERT_TRUE(val3.has_value());
  EXPECT_EQ(**val3, 7);
}

TEST_F(LRUCacheTest, MaxSize) {
  TestCache cache(42);
  EXPECT_EQ(cache.MaxSize(), 42u);
  TestCache unlimited(0);
  EXPECT_EQ(unlimited.MaxSize(), 0u);
}

TEST_F(LRUCacheTest, TTLWithCapacityEviction) {
  using namespace std::chrono_literals;
  TestCache cache(2, 100ms);
  cache.Set(1, "one");
  MockClock::Advance(60ms);
  cache.Set(2, "two");
  MockClock::Advance(60ms);
  // Key 1 is now 120ms old (expired), key 2 is 60ms old (not expired).
  // Inserting key 3 should evict key 1 (LRU), not key 2.
  cache.Set(3, "three");
  // Key 1 was evicted by capacity, but even if accessed it would be expired.
  EXPECT_EQ(cache.Get(1), std::nullopt);
  EXPECT_NE(cache.Get(2), std::nullopt);
  EXPECT_NE(cache.Get(3), std::nullopt);
}

TEST_F(LRUCacheTest, SetAfterErase) {
  TestCache cache(4);
  cache.Set(1, "one");
  cache.Erase(1);
  cache.Set(1, "one_again");
  auto val = cache.Get(1);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "one_again");
}

TEST_F(LRUCacheTest, Empty) {
  TestCache cache(4);
  EXPECT_TRUE(cache.Empty());
  cache.Set(1, "one");
  EXPECT_FALSE(cache.Empty());
  cache.Erase(1);
  EXPECT_TRUE(cache.Empty());
}
