#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <QTimer>
#include <gtest/gtest.h>
#include <qcorotimer.h>

#include "LogSource.h"

namespace lgx {
namespace {

using namespace std::chrono_literals;

PageDataPtr makePage(std::string text) {
  return PageData::fromText(text);
}

QCoro::Task<PageDataPtr> makeDelayedPage(std::string text, int& call_count,
                                         std::chrono::milliseconds delay = 5ms) {
  ++call_count;
  QTimer timer;
  timer.setSingleShot(true);
  timer.start(delay);
  co_await timer;
  co_return makePage(std::move(text));
}

QCoro::Task<PageDataPtr> makeFailingPage(int& call_count,
                                         std::chrono::milliseconds delay = 5ms) {
  ++call_count;
  QTimer timer;
  timer.setSingleShot(true);
  timer.start(delay);
  co_await timer;
  throw std::runtime_error("synthetic load failure");
}

QCoro::Task<std::vector<PageDataPtr>> collectAll(std::vector<QCoro::Task<PageDataPtr>> tasks) {
  std::vector<PageDataPtr> pages;
  pages.reserve(tasks.size());
  for (auto& task : tasks) {
    pages.push_back(co_await std::move(task));
  }
  co_return pages;
}

void expectKey(const PageKey& actual, size_t source_id, size_t page_index) {
  EXPECT_EQ(actual.source_id, source_id);
  EXPECT_EQ(actual.page_index, page_index);
}

TEST(PageCacheTests, BasicHitMissBehavior) {
  GlobalPageCache cache({.max_entries = 8, .max_bytes = 1024 * 1024});
  PageKey key{1, 7};
  int calls = 0;

  const auto first = QCoro::waitFor(cache.getOrLoad(key, [&]() {
    return makeDelayedPage("alpha\nbeta\n", calls);
  }));
  const auto second = QCoro::waitFor(cache.getOrLoad(key, [&]() {
    return makeDelayedPage("unused\n", calls);
  }));

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(first, second);
  EXPECT_EQ(first->lineAt(0), "alpha");

  const auto snapshot = cache.snapshot();
  EXPECT_EQ(snapshot.resident_entries, 1U);
  EXPECT_EQ(snapshot.resident_bytes, first->memoryCost());
  ASSERT_EQ(snapshot.lru_order.size(), 1U);
  expectKey(snapshot.lru_order.front(), 1, 7);
}

TEST(PageCacheTests, ConcurrentSameKeyRequestsCoalesce) {
  GlobalPageCache cache({.max_entries = 8, .max_bytes = 1024 * 1024});
  PageKey key{2, 3};
  int calls = 0;

  std::vector<QCoro::Task<PageDataPtr>> tasks;
  for (int i = 0; i < 5; ++i) {
    tasks.push_back(cache.getOrLoad(key, [&]() { return makeDelayedPage("shared\n", calls); }));
  }

  const auto pages = QCoro::waitFor(collectAll(std::move(tasks)));
  ASSERT_EQ(pages.size(), 5U);
  EXPECT_EQ(calls, 1);
  for (const auto& page : pages) {
    EXPECT_EQ(page, pages.front());
    ASSERT_TRUE(page);
    EXPECT_EQ(page->lineAt(0), "shared");
  }
  EXPECT_EQ(cache.entryState(key), EntryState::Ready);
}

TEST(PageCacheTests, DifferentKeysCanLoadIndependently) {
  GlobalPageCache cache({.max_entries = 8, .max_bytes = 1024 * 1024});
  int calls_a = 0;
  int calls_b = 0;

  std::vector<QCoro::Task<PageDataPtr>> tasks;
  tasks.push_back(cache.getOrLoad(PageKey{3, 0}, [&]() { return makeDelayedPage("a0\n", calls_a); }));
  tasks.push_back(cache.getOrLoad(PageKey{3, 1}, [&]() { return makeDelayedPage("b0\n", calls_b); }));

  const auto pages = QCoro::waitFor(collectAll(std::move(tasks)));
  ASSERT_EQ(pages.size(), 2U);
  EXPECT_EQ(calls_a, 1);
  EXPECT_EQ(calls_b, 1);
  EXPECT_NE(pages[0], pages[1]);
  EXPECT_EQ(cache.snapshot().resident_entries, 2U);
}

TEST(PageCacheTests, LruOrderingUpdatesOnHit) {
  GlobalPageCache cache({.max_entries = 8, .max_bytes = 1024 * 1024});

  QCoro::waitFor(cache.getOrLoad(PageKey{4, 0}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("A\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{4, 1}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("B\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{4, 2}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("C\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{4, 0}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("unused\n");
  }));

  const auto snapshot = cache.snapshot();
  ASSERT_EQ(snapshot.lru_order.size(), 3U);
  expectKey(snapshot.lru_order[0], 4, 0);
  expectKey(snapshot.lru_order[1], 4, 2);
  expectKey(snapshot.lru_order[2], 4, 1);
}

TEST(PageCacheTests, EvictionSkipsReferencedPages) {
  GlobalPageCache cache({.max_entries = 2, .max_bytes = 1024 * 1024});

  const auto a = QCoro::waitFor(cache.getOrLoad(PageKey{5, 0}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("A\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{5, 1}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("B\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{5, 2}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("C\n");
  }));

  ASSERT_TRUE(a);
  EXPECT_TRUE(cache.contains(PageKey{5, 0}));
  EXPECT_FALSE(cache.contains(PageKey{5, 1}));
  EXPECT_TRUE(cache.contains(PageKey{5, 2}));
}

TEST(PageCacheTests, TrimScansBackwardInOnePass) {
  GlobalPageCache cache({.max_entries = 4, .max_bytes = 1024 * 1024});

  const auto a = QCoro::waitFor(cache.getOrLoad(PageKey{6, 0}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("A\n");
  }));
  const auto b = QCoro::waitFor(cache.getOrLoad(PageKey{6, 1}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("B\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{6, 2}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("C\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{6, 3}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("D\n");
  }));

  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  cache.setConfig({.max_entries = 2, .max_bytes = 1024 * 1024});

  const auto snapshot = cache.snapshot();
  EXPECT_EQ(snapshot.resident_entries, 2U);
  EXPECT_TRUE(cache.contains(PageKey{6, 0}));
  EXPECT_TRUE(cache.contains(PageKey{6, 1}));
  EXPECT_FALSE(cache.contains(PageKey{6, 2}));
  EXPECT_FALSE(cache.contains(PageKey{6, 3}));
}

TEST(PageCacheTests, PageIsEvictedWhenNoLongerReferenced) {
  GlobalPageCache cache({.max_entries = 1, .max_bytes = 1024 * 1024});

  auto page = QCoro::waitFor(cache.getOrLoad(PageKey{7, 0}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("A\n");
  }));

  cache.setConfig({.max_entries = 0, .max_bytes = 0});
  EXPECT_TRUE(cache.contains(PageKey{7, 0}));

  page.reset();
  cache.trim();
  EXPECT_FALSE(cache.contains(PageKey{7, 0}));
  EXPECT_EQ(cache.snapshot().resident_entries, 0U);
}

TEST(PageCacheTests, LoadFailureWakesAllWaiters) {
  GlobalPageCache cache({.max_entries = 8, .max_bytes = 1024 * 1024});
  PageKey key{8, 0};
  int calls = 0;

  auto task1 = cache.getOrLoad(key, [&]() { return makeFailingPage(calls); });
  auto task2 = cache.getOrLoad(key, [&]() { return makeFailingPage(calls); });
  auto task3 = cache.getOrLoad(key, [&]() { return makeFailingPage(calls); });

  EXPECT_THROW(QCoro::waitFor(std::move(task1)), std::runtime_error);
  EXPECT_THROW(QCoro::waitFor(std::move(task2)), std::runtime_error);
  EXPECT_THROW(QCoro::waitFor(std::move(task3)), std::runtime_error);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(cache.entryState(key), EntryState::Failed);
}

TEST(PageCacheTests, RetryAfterFailure) {
  GlobalPageCache cache({.max_entries = 8, .max_bytes = 1024 * 1024});
  PageKey key{9, 0};
  int failing_calls = 0;
  int success_calls = 0;

  EXPECT_THROW(QCoro::waitFor(cache.getOrLoad(key, [&]() { return makeFailingPage(failing_calls); })),
               std::runtime_error);

  const auto page = QCoro::waitFor(cache.getOrLoad(key, [&]() {
    return makeDelayedPage("recovered\n", success_calls);
  }));

  ASSERT_TRUE(page);
  EXPECT_EQ(failing_calls, 1);
  EXPECT_EQ(success_calls, 1);
  EXPECT_EQ(page->lineAt(0), "recovered");
  EXPECT_EQ(cache.entryState(key), EntryState::Ready);
}

TEST(PageCacheTests, ConfigChangeTriggersTrimming) {
  GlobalPageCache cache({.max_entries = 4, .max_bytes = 1024 * 1024});

  const auto pinned = QCoro::waitFor(cache.getOrLoad(PageKey{10, 0}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("A\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{10, 1}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("B\n");
  }));
  QCoro::waitFor(cache.getOrLoad(PageKey{10, 2}, [&]() -> QCoro::Task<PageDataPtr> {
    co_return makePage("C\n");
  }));

  ASSERT_TRUE(pinned);
  cache.setConfig({.max_entries = 1, .max_bytes = 1024 * 1024});

  EXPECT_TRUE(cache.contains(PageKey{10, 0}));
  EXPECT_EQ(cache.snapshot().resident_entries, 1U);
}

TEST(PageCacheTests, PublishedSnapshotRemainsValidAfterInvalidation) {
  GlobalPageCache cache({.max_entries = 8, .max_bytes = 1024 * 1024});
  PageKey key{11, 0};

  const auto page = QCoro::waitFor(cache.getOrLoad(key, [&]() -> QCoro::Task<PageDataPtr> {
    co_return PageData::fromText("first\nsecond\n");
  }));

  ASSERT_TRUE(page);
  cache.invalidate(key);
  cache.trim();

  EXPECT_FALSE(cache.contains(key));
  EXPECT_EQ(page->lineAt(0), "first");
  EXPECT_EQ(page->lineAt(1), "second");
}

TEST(PageCacheTests, FrontPageReplacementProducesNewSnapshotWithoutMutatingOldOne) {
  GlobalPageCache cache({.max_entries = 8, .max_bytes = 1024 * 1024});
  PageKey key{12, 0};
  int calls = 0;

  const auto v1 = QCoro::waitFor(cache.getOrLoad(key, [&]() {
    ++calls;
    return []() -> QCoro::Task<PageDataPtr> { co_return PageData::fromText("old\n"); }();
  }));

  cache.invalidate(key);

  const auto v2 = QCoro::waitFor(cache.getOrLoad(key, [&]() {
    ++calls;
    return []() -> QCoro::Task<PageDataPtr> { co_return PageData::fromText("new\n"); }();
  }));

  ASSERT_TRUE(v1);
  ASSERT_TRUE(v2);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(v1->lineAt(0), "old");
  EXPECT_EQ(v2->lineAt(0), "new");
  EXPECT_NE(v1, v2);
}

TEST(PageCacheTests, LogSourceUsesSingleSharedCacheInstance) {
  class DummySource final : public LogSource {};

  DummySource a;
  DummySource b;

  auto& cache_a = a.sharedPageCache();
  auto& cache_b = b.sharedPageCache();

  EXPECT_EQ(&cache_a, &cache_b);
  EXPECT_NE(a.sourceId(), b.sourceId());
}

}  // namespace
}  // namespace lgx
