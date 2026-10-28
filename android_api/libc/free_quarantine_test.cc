#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

extern "C" void __wrap_free(void* ptr);

namespace {
std::atomic<size_t> g_free_calls{0};
std::atomic<void*> g_last_freed{nullptr};
constexpr size_t kRingSize = 1024;

void FreeSmallAllocations(size_t count) {
  for (size_t i = 0; i < count; ++i) {
    void* ptr = malloc(64);
    ASSERT_NE(ptr, nullptr);
    __wrap_free(ptr);
  }
}
}  // namespace

extern "C" void __real_free(void* ptr) {
  g_last_freed.store(ptr);
  g_free_calls.fetch_add(1);
  free(ptr);
}

TEST(FreeQuarantineTest, ForwardsNull) {
  const size_t before = g_free_calls.load();
  __wrap_free(nullptr);
  EXPECT_EQ(g_free_calls.load(), before + 1);
  EXPECT_EQ(g_last_freed.load(), nullptr);
}

TEST(FreeQuarantineTest, ReleasesLargeAllocationsImmediately) {
  for (size_t size : {size_t{1024 * 1024 + 1}, size_t{16 * 1024 * 1024}}) {
    void* ptr = malloc(size);
    ASSERT_NE(ptr, nullptr);
    const size_t before = g_free_calls.load();
    __wrap_free(ptr);
    EXPECT_EQ(g_free_calls.load(), before + 1);
    EXPECT_EQ(g_last_freed.load(), ptr);
  }
}

TEST(FreeQuarantineTest, PreservesAllocationsBelowLimitUntilRingEviction) {
  for (size_t size : {size_t{64}, size_t{128 * 1024}, size_t{1024 * 1024 - 4096}}) {
    void* ptr = malloc(size);
    ASSERT_NE(ptr, nullptr);
    g_last_freed.store(nullptr);
    __wrap_free(ptr);
    EXPECT_NE(g_last_freed.load(), ptr);
    FreeSmallAllocations(kRingSize - 1);
    EXPECT_NE(g_last_freed.load(), ptr);
    FreeSmallAllocations(1);
    EXPECT_EQ(g_last_freed.load(), ptr);
  }
}

TEST(FreeQuarantineTest, ConcurrentEvictionsFreeEachAllocationOnce) {
  FreeSmallAllocations(kRingSize);
  const size_t before = g_free_calls.load();
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 8; ++i) {
    threads.emplace_back([] { FreeSmallAllocations(1024); });
  }
  for (auto& thread : threads) thread.join();
  EXPECT_EQ(g_free_calls.load(), before + 8 * 1024);
}
