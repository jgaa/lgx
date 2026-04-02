#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <qcorotask.h>

#include "PageCache.h"
#include "LogScanner.h"

namespace lgx {

enum class SourceState {
  Idle,
  Opening,
  Indexing,
  Ready,
  Updating,
  ResetDetected,
  Failed
};

enum class SourceResetReason {
  Truncated,
  Recreated,
  Disappeared
};

struct SourceLine {
  uint64_t line_number{};
  LogLevel log_level{LogLevel_Info};
  uint32_t pid{};
  uint32_t tid{};
  std::string text;
  std::string function_name;
  std::string message;
  std::string thread_id;
  std::optional<std::chrono::system_clock::time_point> timestamp;
};

struct SourceLines {
  uint64_t first_line{};
  std::vector<SourceLine> lines;
};

struct SourceSnapshot {
  SourceState state{SourceState::Idle};
  uint64_t line_count{};
  uint64_t indexed_size{};
  uint64_t file_size{};
  bool following{false};
  double lines_per_second{};
};

struct SourceCallbacks {
  std::function<void(SourceSnapshot)> on_state_changed;
  std::function<void(uint64_t first_new_line, uint64_t count)> on_lines_appended;
  std::function<void(SourceResetReason)> on_reset;
  std::function<void(std::string)> on_error;
};

/**
 * @brief Base class for log sources backed by the shared global page cache.
 *
 * LogSource instances own only stable source-local metadata such as line counts
 * and page descriptors. Loaded page payloads are stored in the process-wide
 * GlobalPageCache and accessed through immutable PageData snapshots.
 */
class LogSource {
 public:
  /**
   * @brief Stable metadata for one source page.
   *
   * This struct intentionally excludes resident page bytes. Page payloads live
   * in GlobalPageCache instead.
   */
  class PageMeta {
   public:
    struct LineIndex {
      std::vector<ParsedLineMetadata> lines;
    };

    PageMeta() = default;

    [[nodiscard]] size_t lines() const noexcept { return lines_; }
    [[nodiscard]] size_t firstLine() const noexcept { return first_line_; }
    [[nodiscard]] uint64_t filePos() const noexcept { return file_pos_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t offsetToStartOfLine() const noexcept { return offset_sol_; }
    [[nodiscard]] size_t levelLines(LogLevel level) const noexcept;
    [[nodiscard]] std::chrono::system_clock::time_point timestampStart() const noexcept {
      return ts_start_;
    }

    void setLines(size_t lines) noexcept { lines_ = lines; }
    void setFirstLine(size_t first_line) noexcept { first_line_ = first_line; }
    void setFilePos(uint64_t file_pos) noexcept { file_pos_ = file_pos; }
    void setSize(size_t size) noexcept { size_ = size; }
    void setOffsetToStartOfLine(size_t offset) noexcept { offset_sol_ = offset; }
    void setTimestampStart(std::chrono::system_clock::time_point ts) noexcept {
      ts_start_ = ts;
    }
    void setLevelLines(LogLevel level, size_t lines) noexcept;
    void clearLineIndex() noexcept;
    [[nodiscard]] const LineIndex& ensureLineIndex(
        std::function<std::unique_ptr<LineIndex>()> factory) const;

   private:
    size_t lines_{};
    size_t first_line_{};
    size_t offset_sol_{};
    uint64_t file_pos_{};
    size_t size_{};
    std::array<size_t, number_of_log_levels> level_lines_{};
    std::chrono::system_clock::time_point ts_start_{};
    mutable std::mutex line_index_mutex_;
    mutable std::unique_ptr<LineIndex> line_index_;
  };

  LogSource() = default;
  LogSource(const LogSource&) = delete;
  LogSource& operator=(const LogSource&) = delete;
  virtual ~LogSource() = default;

  virtual void setCallbacks(SourceCallbacks callbacks);
  [[nodiscard]] virtual std::string path() const { return {}; }
  virtual void open(const std::string& path);
  virtual void close() {}
  virtual void startIndexing() {}
  virtual void refresh() {}
  virtual void setFollowing(bool enabled);
  [[nodiscard]] virtual std::string scannerName() const;
  [[nodiscard]] virtual std::string requestedScannerName() const;
  virtual void setRequestedScannerName(std::string name);
  [[nodiscard]] virtual SourceSnapshot snapshot() const;
  [[nodiscard]] virtual uint64_t fileSize() const;
  [[nodiscard]] virtual double linesPerSecond() const;
  virtual void fetchLines(uint64_t first_line, size_t count,
                          std::function<void(SourceLines)> on_ready);
  [[nodiscard]] virtual std::optional<uint64_t> nextLineWithLevel(uint64_t after_line,
                                                                  LogLevel level) const;
  [[nodiscard]] virtual std::optional<uint64_t> previousLineWithLevel(
      uint64_t before_line, LogLevel level) const;

  [[nodiscard]] size_t lines() const noexcept {
    return lines_.load(std::memory_order_relaxed);
  }

  void setLines(size_t lines) noexcept {
    lines_.store(lines, std::memory_order_relaxed);
  }

  [[nodiscard]] size_t sourceId() const noexcept { return source_id_; }

  /**
   * @brief Load one page through the shared global cache.
   *
   * The loader is invoked only when the requested page is not already ready in
   * the shared cache and no other caller is currently loading it.
   *
   * @tparam Loader Callable returning QCoro::Task<PageDataPtr>.
   * @param page_index Zero-based source-local page index.
   * @param loader Page loader callback.
   * @return Shared immutable page snapshot.
   */
  template <typename Loader>
  [[nodiscard]] QCoro::Task<PageDataPtr> loadPageViaCache(size_t page_index, Loader&& loader) const {
    co_return co_await sharedPageCache().getOrLoad(
        PageKey{source_id_, page_index}, std::forward<Loader>(loader));
  }

  /**
   * @brief Access the process-wide shared page cache.
   *
   * All LogSource instances use this cache instance.
   *
   * @return Shared page cache.
   */
  [[nodiscard]] static GlobalPageCache& sharedPageCache();

  /**
   * @brief Update shared page cache limits.
   *
   * @param config New cache configuration.
   */
  static void setSharedPageCacheConfig(GlobalPageCache::Config config);

 protected:
  [[nodiscard]] std::deque<PageMeta>& pageMetadata() noexcept { return pages_; }
  [[nodiscard]] const std::deque<PageMeta>& pageMetadata() const noexcept { return pages_; }

  void emitStateChanged(SourceSnapshot snapshot) const;
  void emitLinesAppended(uint64_t first_new_line, uint64_t count) const;
  void emitReset(SourceResetReason reason) const;
  void emitError(std::string message) const;
  void updateLinesPerSecond(std::chrono::steady_clock::time_point now, uint64_t appended_lines);
  [[nodiscard]] double currentLinesPerSecond(std::chrono::steady_clock::time_point now) const;

 private:
  struct AppendRateSample {
    std::chrono::steady_clock::time_point timestamp;
    uint64_t lines{};
  };

  static constexpr auto kLinesPerSecondWindow = std::chrono::seconds(10);
  static std::atomic_size_t source_id_feed_;

  std::atomic_size_t lines_{};
  std::deque<PageMeta> pages_;
  const size_t source_id_{source_id_feed_.fetch_add(1, std::memory_order_relaxed) + 1};
  mutable std::mutex lines_per_second_mutex_;
  mutable std::deque<AppendRateSample> append_rate_samples_;
  mutable std::mutex callbacks_mutex_;
  SourceCallbacks callbacks_;
};

}  // namespace lgx
