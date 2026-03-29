#include "PageCache.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace lgx {

namespace {

size_t combineHash(size_t lhs, size_t rhs) noexcept {
  lhs ^= rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6U) + (lhs >> 2U);
  return lhs;
}

std::vector<uint32_t> lineOffsetsFromText(std::string_view text) {
  std::vector<uint32_t> offsets;
  if (text.empty()) {
    return offsets;
  }

  offsets.push_back(0);
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n' && i + 1U < text.size()) {
      offsets.push_back(static_cast<uint32_t>(i + 1U));
    }
  }

  return offsets;
}

void resumeWaiters(std::vector<std::coroutine_handle<>> waiters) {
  for (auto waiter : waiters) {
    if (waiter) {
      waiter.resume();
    }
  }
}

}  // namespace

size_t PageKeyHash::operator()(const PageKey& key) const noexcept {
  return combineHash(std::hash<size_t>{}(key.source_id), std::hash<size_t>{}(key.page_index));
}

PageData::PageData(std::shared_ptr<std::vector<char>> storage, size_t visible_size,
                   std::vector<uint32_t> line_offsets, std::vector<LogLevel> levels)
    : storage_(std::move(storage)),
      visible_size_(visible_size),
      line_offsets_(std::move(line_offsets)),
      levels_(std::move(levels)) {
  if (!storage_) {
    throw std::invalid_argument("PageData storage must not be null");
  }
  if (visible_size_ > storage_->size()) {
    throw std::invalid_argument("PageData visible_size exceeds storage");
  }
  if (levels_.size() != line_offsets_.size()) {
    throw std::invalid_argument("PageData levels size must match line offsets");
  }
}

std::shared_ptr<const PageData> PageData::fromText(std::string_view text,
                                                   std::vector<LogLevel> levels) {
  auto storage = std::make_shared<std::vector<char>>(text.begin(), text.end());
  auto offsets = lineOffsetsFromText(text);

  if (!text.empty() && offsets.empty()) {
    offsets.push_back(0);
  }

  if (levels.empty()) {
    levels.resize(offsets.size(), LogLevel_Info);
  }

  return std::make_shared<const PageData>(std::move(storage), text.size(), std::move(offsets),
                                          std::move(levels));
}

std::span<const char> PageData::bytes() const noexcept {
  return {storage_->data(), visible_size_};
}

size_t PageData::lineCount() const noexcept {
  return line_offsets_.size();
}

std::string_view PageData::lineAt(size_t index) const noexcept {
  if (index >= line_offsets_.size()) {
    return {};
  }

  const size_t begin = line_offsets_[index];
  size_t end = visible_size_;
  if (index + 1U < line_offsets_.size()) {
    end = line_offsets_[index + 1U];
  }

  while (end > begin && ((*storage_)[end - 1U] == '\n' || (*storage_)[end - 1U] == '\r')) {
    --end;
  }

  return {storage_->data() + begin, end - begin};
}

LogLevel PageData::levelAt(size_t index) const noexcept {
  if (index >= levels_.size()) {
    return LogLevel_Info;
  }
  return levels_[index];
}

size_t PageData::memoryCost() const noexcept {
  return storage_->size() + line_offsets_.capacity() * sizeof(uint32_t) +
         levels_.capacity() * sizeof(LogLevel);
}

std::string_view RangeResult::lineAt(size_t index) const noexcept {
  if (index >= lines.size()) {
    return {};
  }

  const auto ref = lines[index];
  if (ref.page_slot >= pages.size() || !pages[ref.page_slot]) {
    return {};
  }

  return pages[ref.page_slot]->lineAt(ref.line_index_in_page);
}

class GlobalPageCache::LoadAwaiter {
 public:
  LoadAwaiter(GlobalPageCache& cache, std::shared_ptr<LoadState> load_state)
      : cache_(cache), load_state_(std::move(load_state)) {}

  bool await_ready() const noexcept {
    std::lock_guard lock(cache_.mutex_);
    return load_state_->done;
  }

  bool await_suspend(std::coroutine_handle<> handle) {
    std::lock_guard lock(cache_.mutex_);
    if (load_state_->done) {
      return false;
    }

    load_state_->waiters.push_back(handle);
    return true;
  }

  PageDataPtr await_resume() {
    std::lock_guard lock(cache_.mutex_);
    if (load_state_->error) {
      std::rethrow_exception(load_state_->error);
    }
    return load_state_->result;
  }

 private:
  GlobalPageCache& cache_;
  std::shared_ptr<LoadState> load_state_;
};

GlobalPageCache::GlobalPageCache()
    : GlobalPageCache(Config{}) {}

GlobalPageCache::GlobalPageCache(Config config)
    : config_(config) {}

void GlobalPageCache::setConfig(Config config) {
  {
    std::lock_guard lock(mutex_);
    config_ = config;
  }

  trim();
}

GlobalPageCache::Config GlobalPageCache::config() const noexcept {
  std::lock_guard lock(mutex_);
  return config_;
}

void GlobalPageCache::trim() {
  std::lock_guard lock(mutex_);
  for (CacheEntry* cursor = lru_tail_; cursor != nullptr && overBudgetLocked();) {
    CacheEntry* previous = cursor->lru_prev;
    const auto it = entries_.find(cursor->key);
    if (it != entries_.end() && it->second.get() == cursor && cursor->state == EntryState::Ready &&
        cursor->page && cursor->page.use_count() == 1U) {
      eraseReadyEntryLocked(it);
    }
    cursor = previous;
  }
}

void GlobalPageCache::clearEvictable() {
  std::lock_guard lock(mutex_);
  for (CacheEntry* cursor = lru_tail_; cursor != nullptr;) {
    CacheEntry* previous = cursor->lru_prev;
    const auto it = entries_.find(cursor->key);
    if (it != entries_.end() && it->second.get() == cursor && cursor->state == EntryState::Ready &&
        cursor->page && cursor->page.use_count() == 1U) {
      eraseReadyEntryLocked(it);
    }
    cursor = previous;
  }
}

void GlobalPageCache::invalidate(PageKey key) {
  std::lock_guard lock(mutex_);
  const auto it = entries_.find(key);
  if (it == entries_.end()) {
    return;
  }

  auto& entry = *it->second;
  if (entry.state == EntryState::Ready && entry.page) {
    eraseReadyEntryLocked(it);
    return;
  }

  if (entry.state != EntryState::Loading) {
    entries_.erase(it);
  }
}

bool GlobalPageCache::contains(PageKey key) const {
  std::lock_guard lock(mutex_);
  const auto it = entries_.find(key);
  return it != entries_.end() && it->second && it->second->state == EntryState::Ready &&
         static_cast<bool>(it->second->page);
}

EntryState GlobalPageCache::entryState(PageKey key) const {
  std::lock_guard lock(mutex_);
  const auto it = entries_.find(key);
  if (it == entries_.end() || !it->second) {
    return EntryState::Empty;
  }

  return it->second->state;
}

GlobalPageCache::Snapshot GlobalPageCache::snapshot() const {
  std::lock_guard lock(mutex_);

  Snapshot snapshot;
  snapshot.config = config_;
  snapshot.resident_entries = resident_entries_;
  snapshot.resident_bytes = resident_bytes_;
  for (auto* entry = lru_head_; entry != nullptr; entry = entry->lru_next) {
    snapshot.lru_order.push_back(entry->key);
  }

  return snapshot;
}

void GlobalPageCache::lruPushFront(CacheEntry& entry) {
  if (entry.on_lru) {
    return;
  }

  entry.on_lru = true;
  entry.lru_prev = nullptr;
  entry.lru_next = lru_head_;
  if (lru_head_) {
    lru_head_->lru_prev = &entry;
  } else {
    lru_tail_ = &entry;
  }
  lru_head_ = &entry;
}

void GlobalPageCache::lruRemove(CacheEntry& entry) {
  if (!entry.on_lru) {
    return;
  }

  if (entry.lru_prev) {
    entry.lru_prev->lru_next = entry.lru_next;
  } else {
    lru_head_ = entry.lru_next;
  }

  if (entry.lru_next) {
    entry.lru_next->lru_prev = entry.lru_prev;
  } else {
    lru_tail_ = entry.lru_prev;
  }

  entry.on_lru = false;
  entry.lru_prev = nullptr;
  entry.lru_next = nullptr;
}

void GlobalPageCache::lruTouch(CacheEntry& entry) {
  if (entry.on_lru && lru_head_ == &entry) {
    return;
  }

  lruRemove(entry);
  lruPushFront(entry);
}

QCoro::Task<PageDataPtr> GlobalPageCache::awaitLoad(
    const std::shared_ptr<LoadState>& load_state) {
  co_return co_await LoadAwaiter{*this, load_state};
}

void GlobalPageCache::publishReady(const std::shared_ptr<CacheEntry>& entry, PageDataPtr page) {
  std::vector<std::coroutine_handle<>> waiters;
  bool should_trim = false;

  {
    std::lock_guard lock(mutex_);
    if (entry->state == EntryState::Ready && entry->page) {
      resident_bytes_ -= entry->cost_bytes;
      if (resident_entries_ > 0U) {
        --resident_entries_;
      }
    }

    entry->page = std::move(page);
    entry->cost_bytes = entry->page->memoryCost();
    entry->state = EntryState::Ready;
    entry->error = nullptr;
    noteRequestLocked(*entry);

    ++resident_entries_;
    resident_bytes_ += entry->cost_bytes;

    if (entry->load_state) {
      entry->load_state->done = true;
      entry->load_state->result = entry->page;
      waiters.swap(entry->load_state->waiters);
      entry->load_state.reset();
    }

    should_trim = overBudgetLocked();
  }

  resumeWaiters(std::move(waiters));

  if (should_trim) {
    trim();
  }
}

void GlobalPageCache::publishFailure(const std::shared_ptr<CacheEntry>& entry,
                                     std::exception_ptr error) {
  std::vector<std::coroutine_handle<>> waiters;

  {
    std::lock_guard lock(mutex_);
    entry->state = EntryState::Failed;
    entry->error = error;
    if (entry->load_state) {
      entry->load_state->done = true;
      entry->load_state->error = error;
      waiters.swap(entry->load_state->waiters);
      entry->load_state.reset();
    }
  }

  resumeWaiters(std::move(waiters));
}

bool GlobalPageCache::overBudgetLocked() const noexcept {
  return resident_entries_ > config_.max_entries || resident_bytes_ > config_.max_bytes;
}

void GlobalPageCache::noteRequestLocked(CacheEntry& entry) {
  entry.last_request_tick = ++request_tick_;
  lruTouch(entry);
}

void GlobalPageCache::eraseReadyEntryLocked(const EntryMap::iterator& it) {
  auto& entry = *it->second;
  if (entry.state == EntryState::Ready && entry.page) {
    lruRemove(entry);
    resident_bytes_ -= entry.cost_bytes;
    if (resident_entries_ > 0U) {
      --resident_entries_;
    }
  }
  entries_.erase(it);
}

}  // namespace lgx
