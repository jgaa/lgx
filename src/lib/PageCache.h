#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <qcorotask.h>

#include "LogTypes.h"

namespace lgx {

/**
 * @brief Unique key for a cached page.
 *
 * A page is uniquely identified by the log source id and the zero-based page
 * index within that source.
 */
struct PageKey {
  size_t source_id{};
  size_t page_index{};

  /**
   * @brief Equality comparison.
   *
   * @param other Other key.
   * @return True when source id and page index are equal.
   */
  bool operator==(const PageKey& other) const noexcept = default;
};

/**
 * @brief Hash functor for PageKey.
 */
struct PageKeyHash {
  /**
   * @brief Compute hash value for a page key.
   *
   * @param key Page key to hash.
   * @return Hash value.
   */
  [[nodiscard]] size_t operator()(const PageKey& key) const noexcept;
};

/**
 * @brief Immutable snapshot of a loaded page.
 *
 * A PageData object contains the bytes visible at the time it was published,
 * plus parsed indexing information for line-level access. Instances are shared
 * across threads via std::shared_ptr<const PageData>.
 */
class PageData {
 public:
  /**
   * @brief Construct a page snapshot from already prepared storage.
   *
   * @param storage Shared storage owning the page bytes.
   * @param visible_size Number of visible bytes in storage.
   * @param line_offsets Offsets of page-local line starts.
   * @param levels Parsed log level for each line.
   */
  PageData(std::shared_ptr<std::vector<char>> storage, size_t visible_size,
           std::vector<uint32_t> line_offsets, std::vector<LogLevel> levels);

  /**
   * @brief Build a snapshot from plain text lines for tests and simple loaders.
   *
   * Each detected line receives the matching entry from @p levels, or
   * LogLevel_Info when the level vector is omitted.
   *
   * @param text Visible page text.
   * @param levels Optional per-line log levels.
   * @return Shared immutable page snapshot.
   */
  [[nodiscard]] static std::shared_ptr<const PageData> fromText(
      std::string_view text, std::vector<LogLevel> levels = {});

  /**
   * @brief Get visible page bytes as a span.
   *
   * The returned span remains valid while this PageData instance stays alive.
   *
   * @return Span of visible bytes.
   */
  [[nodiscard]] std::span<const char> bytes() const noexcept;

  /**
   * @brief Get number of parsed lines in this page.
   *
   * @return Number of lines.
   */
  [[nodiscard]] size_t lineCount() const noexcept;

  /**
   * @brief Get line text by page-local line index.
   *
   * Trailing newline and carriage-return characters are excluded.
   *
   * @param index Zero-based line index within this page.
   * @return View of the requested line.
   */
  [[nodiscard]] std::string_view lineAt(size_t index) const noexcept;

  /**
   * @brief Get log level for a page-local line index.
   *
   * @param index Zero-based line index within this page.
   * @return Parsed log level.
   */
  [[nodiscard]] LogLevel levelAt(size_t index) const noexcept;

  /**
   * @brief Estimate memory cost of this page object.
   *
   * Includes payload bytes and parsed auxiliary structures.
   *
   * @return Approximate memory usage in bytes.
   */
  [[nodiscard]] size_t memoryCost() const noexcept;

 private:
  std::shared_ptr<std::vector<char>> storage_;
  size_t visible_size_{};
  std::vector<uint32_t> line_offsets_;
  std::vector<LogLevel> levels_;
};

using PageDataPtr = std::shared_ptr<const PageData>;

/**
 * @brief Result of a line-range request assembled from one or more pages.
 */
struct LineRef {
  uint32_t page_slot{};
  uint32_t line_index_in_page{};
};

/**
 * @brief Materialized page range with line lookup metadata.
 */
struct RangeResult {
  std::vector<PageDataPtr> pages;
  std::vector<LineRef> lines;

  /**
   * @brief Read one logical line from the assembled range.
   *
   * @param index Zero-based line index within the range.
   * @return View of the requested line.
   */
  [[nodiscard]] std::string_view lineAt(size_t index) const noexcept;
};

/**
 * @brief Internal cache entry state.
 */
enum class EntryState {
  Empty,
  Loading,
  Ready,
  Failed
};

/**
 * @brief Shared state for an in-flight page load.
 *
 * All concurrent requesters for the same page await the same LoadState.
 */
struct LoadState {
  bool done{false};
  PageDataPtr result;
  std::exception_ptr error;
  std::vector<std::coroutine_handle<>> waiters;
};

/**
 * @brief Internal per-key cache entry.
 *
 * One CacheEntry exists for each page key currently known to the cache. The
 * entry coordinates loading, stores the resident page snapshot, and
 * participates in the intrusive LRU list.
 */
struct CacheEntry {
  PageKey key{};
  EntryState state{EntryState::Empty};

  PageDataPtr page;
  std::exception_ptr error;

  uint64_t last_request_tick{0};
  size_t cost_bytes{0};

  std::shared_ptr<LoadState> load_state;

  bool on_lru{false};
  CacheEntry* lru_prev{nullptr};
  CacheEntry* lru_next{nullptr};
};

/**
 * @brief Shared LRU cache for loaded log pages.
 *
 * The cache is keyed by {source_id, page_index} and is safe for concurrent use
 * from worker threads and UI-triggered QCoro requests.
 *
 * The cache guarantees that only one load is active per key at a time.
 * Concurrent requesters for the same page await the same in-flight load.
 *
 * Pages currently referenced outside the cache are not evicted. This means the
 * cache may temporarily remain above its configured limits when all older pages
 * are pinned by external shared_ptr references.
 */
class GlobalPageCache {
 public:
  /**
   * @brief Runtime configuration for the global page cache.
   */
  struct Config {
    /**
     * @brief Maximum number of resident ready entries allowed in the cache.
     */
    size_t max_entries{1024};

    /**
     * @brief Maximum total memory budget in bytes for resident ready pages.
     */
    size_t max_bytes{256 * 1024 * 1024};
  };

  /**
   * @brief Lightweight diagnostics for tests and debugging.
   */
  struct Snapshot {
    Config config;
    size_t resident_entries{};
    size_t resident_bytes{};
    std::vector<PageKey> lru_order;
  };

  /**
   * @brief Construct a cache with the given configuration.
   *
   * @param config Initial cache configuration.
   */
  GlobalPageCache();
  explicit GlobalPageCache(Config config);

  /**
   * @brief Update cache configuration.
   *
   * This may trigger trimming if the new limits are lower than the current
   * resident usage.
   *
   * @param config New configuration.
   */
  void setConfig(Config config);

  /**
   * @brief Get current cache configuration.
   *
   * @return Current configuration snapshot.
   */
  [[nodiscard]] Config config() const noexcept;

  /**
   * @brief Request a page from the cache or load it on demand.
   *
   * If the page is already resident and ready, it is returned immediately.
   * If another thread is already loading the page, this call awaits the same
   * in-flight load.
   * Otherwise, this call becomes the loader and invokes the provided loader.
   *
   * The loader must perform file I/O and parsing outside the cache mutex.
   *
   * The retry policy after failure is "retry on next request": a later call may
   * transition a failed entry back to Loading and attempt the load again.
   *
   * @tparam Loader Callable returning QCoro::Task<PageDataPtr>.
   * @param key Cache key.
   * @param loader Loader callback used only if the page is not already ready and
   *               no load is currently in progress.
   * @return Loaded immutable page snapshot.
   */
  template <typename Loader>
  [[nodiscard]] QCoro::Task<PageDataPtr> getOrLoad(PageKey key, Loader&& loader);

  /**
   * @brief Trim the cache to satisfy configured limits where possible.
   *
   * Pages still referenced outside the cache are skipped and not evicted. The
   * trim pass scans backward from the current LRU tail and inspects each entry
   * at most once in that pass.
   */
  void trim();

  /**
   * @brief Remove all evictable resident entries from the cache.
   *
   * Referenced pages remain alive until the last external shared_ptr reference
   * is released.
   */
  void clearEvictable();

  /**
   * @brief Invalidate a resident entry so a later request loads a fresh snapshot.
   *
   * Existing shared_ptr snapshots remain valid. This is intended for cases such
   * as front-page refresh where callers need future requests to observe a newer
   * snapshot while older readers continue using the old one safely.
   *
   * Active loads are not cancelled.
   *
   * @param key Cache key to invalidate.
   */
  void invalidate(PageKey key);

  /**
   * @brief Check whether a ready resident entry exists for @p key.
   *
   * @param key Cache key.
   * @return True when a ready resident snapshot exists.
   */
  [[nodiscard]] bool contains(PageKey key) const;

  /**
   * @brief Inspect one entry state for tests and diagnostics.
   *
   * @param key Cache key.
   * @return Current state, or EntryState::Empty if no entry exists.
   */
  [[nodiscard]] EntryState entryState(PageKey key) const;

  /**
   * @brief Capture resident counters and LRU order for tests and diagnostics.
   *
   * @return Snapshot of current cache state.
   */
  [[nodiscard]] Snapshot snapshot() const;

 private:
  class LoadAwaiter;

  using EntryMap = boost::unordered_flat_map<PageKey, std::shared_ptr<CacheEntry>, PageKeyHash>;

  /**
   * @brief Insert an entry at the front of the LRU list.
   *
   * @param entry Entry to insert.
   */
  void lruPushFront(CacheEntry& entry);

  /**
   * @brief Remove an entry from the LRU list.
   *
   * @param entry Entry to remove.
   */
  void lruRemove(CacheEntry& entry);

  /**
   * @brief Move an entry to the front of the LRU list.
   *
   * If the entry is not yet on the list, it is inserted.
   *
   * @param entry Entry to promote.
   */
  void lruTouch(CacheEntry& entry);

  /**
   * @brief Await an existing in-flight load.
   *
   * @param load_state Shared in-flight load state.
   * @return Loaded page snapshot, or rethrows the stored load exception.
   */
  [[nodiscard]] QCoro::Task<PageDataPtr> awaitLoad(const std::shared_ptr<LoadState>& load_state);

  /**
   * @brief Publish a successful load result.
   *
   * The page is published as the current resident snapshot, waiters are resumed,
   * and the cache is trimmed if limits are exceeded.
   *
   * @param entry Entry being published.
   * @param page Newly loaded page snapshot.
   */
  void publishReady(const std::shared_ptr<CacheEntry>& entry, PageDataPtr page);

  /**
   * @brief Publish a failed load result and wake waiters.
   *
   * @param entry Entry being published.
   * @param error Error to publish.
   */
  void publishFailure(const std::shared_ptr<CacheEntry>& entry, std::exception_ptr error);

  /**
   * @brief Return whether the cache currently exceeds configured limits.
   *
   * This method expects the cache mutex to be held.
   *
   * @return True when resident counts exceed the configured budget.
   */
  [[nodiscard]] bool overBudgetLocked() const noexcept;

  /**
   * @brief Mark one ready entry as recently requested.
   *
   * This method expects the cache mutex to be held.
   *
   * @param entry Ready resident entry to promote.
   */
  void noteRequestLocked(CacheEntry& entry);

  /**
   * @brief Remove one resident ready entry from the cache.
   *
   * This method expects the cache mutex to be held.
   *
   * @param it Iterator pointing at the entry to erase.
   */
  void eraseReadyEntryLocked(const EntryMap::iterator& it);

  mutable std::mutex mutex_;
  Config config_;
  EntryMap entries_;
  CacheEntry* lru_head_{nullptr};
  CacheEntry* lru_tail_{nullptr};
  uint64_t request_tick_{0};
  size_t resident_entries_{0};
  size_t resident_bytes_{0};
};

template <typename Loader>
QCoro::Task<PageDataPtr> GlobalPageCache::getOrLoad(PageKey key, Loader&& loader) {
  std::shared_ptr<CacheEntry> entry;
  std::shared_ptr<LoadState> load_state;
  bool should_load = false;

  {
    std::lock_guard lock(mutex_);
    auto [it, inserted] = entries_.try_emplace(key);
    if (inserted || !it->second) {
      it->second = std::make_shared<CacheEntry>();
      it->second->key = key;
    }

    entry = it->second;
    if (entry->state == EntryState::Ready && entry->page) {
      noteRequestLocked(*entry);
      co_return entry->page;
    }

    if (entry->state == EntryState::Loading && entry->load_state) {
      load_state = entry->load_state;
    } else {
      entry->state = EntryState::Loading;
      entry->error = nullptr;
      entry->load_state = std::make_shared<LoadState>();
      load_state = entry->load_state;
      should_load = true;
    }
  }

  if (!should_load) {
    co_return co_await awaitLoad(load_state);
  }

  try {
    auto page = co_await std::forward<Loader>(loader)();
    if (!page) {
      throw std::runtime_error("GlobalPageCache loader returned null page");
    }
    publishReady(entry, std::move(page));
  } catch (...) {
    auto error = std::current_exception();
    publishFailure(entry, error);
    std::rethrow_exception(error);
  }

  co_return co_await awaitLoad(load_state);
}

}  // namespace lgx
