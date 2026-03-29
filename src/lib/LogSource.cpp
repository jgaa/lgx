#include "LogSource.h"

#include <cassert>

namespace lgx {

std::atomic_size_t LogSource::source_id_feed_{0};

size_t LogSource::PageMeta::levelLines(LogLevel level) const noexcept {
  assert(level >= LogLevel_Error && level <= LogLevel_Trace);
  return level_lines_[static_cast<size_t>(level)];
}

void LogSource::PageMeta::setLevelLines(LogLevel level, size_t lines) noexcept {
  assert(level >= LogLevel_Error && level <= LogLevel_Trace);
  level_lines_[static_cast<size_t>(level)] = lines;
}

void LogSource::PageMeta::clearLineIndex() noexcept {
  std::lock_guard lock(line_index_mutex_);
  line_index_.reset();
}

const LogSource::PageMeta::LineIndex& LogSource::PageMeta::ensureLineIndex(
    std::function<std::unique_ptr<LineIndex>()> factory) const {
  std::lock_guard lock(line_index_mutex_);
  if (!line_index_) {
    line_index_ = factory();
    if (!line_index_) {
      line_index_ = std::make_unique<LineIndex>();
    }
  }
  return *line_index_;
}

GlobalPageCache& LogSource::sharedPageCache() {
  static GlobalPageCache cache;
  return cache;
}

void LogSource::setCallbacks(SourceCallbacks callbacks) {
  std::lock_guard lock(callbacks_mutex_);
  callbacks_ = std::move(callbacks);
}

void LogSource::open(const std::string&) {
  emitError("LogSource::open is not implemented for this source type");
}

void LogSource::setSharedPageCacheConfig(GlobalPageCache::Config config) {
  sharedPageCache().setConfig(config);
}

void LogSource::setFollowing(bool) {}

std::string LogSource::scannerName() const {
  return {};
}

std::string LogSource::requestedScannerName() const {
  return {};
}

void LogSource::setRequestedScannerName(std::string) {}

SourceSnapshot LogSource::snapshot() const {
  return SourceSnapshot{
      .line_count = lines(),
      .file_size = fileSize(),
      .lines_per_second = linesPerSecond(),
  };
}

uint64_t LogSource::fileSize() const {
  return 0;
}

double LogSource::linesPerSecond() const {
  return currentLinesPerSecond(std::chrono::steady_clock::now());
}

void LogSource::fetchLines(uint64_t, size_t, std::function<void(SourceLines)> on_ready) {
  if (on_ready) {
    on_ready({});
  }
}

std::optional<uint64_t> LogSource::nextLineWithLevel(uint64_t, LogLevel) const {
  return std::nullopt;
}

std::optional<uint64_t> LogSource::previousLineWithLevel(uint64_t, LogLevel) const {
  return std::nullopt;
}

void LogSource::emitStateChanged(SourceSnapshot snapshot) const {
  std::lock_guard lock(callbacks_mutex_);
  if (callbacks_.on_state_changed) {
    callbacks_.on_state_changed(snapshot);
  }
}

void LogSource::emitLinesAppended(uint64_t first_new_line, uint64_t count) const {
  std::lock_guard lock(callbacks_mutex_);
  if (callbacks_.on_lines_appended) {
    callbacks_.on_lines_appended(first_new_line, count);
  }
}

void LogSource::emitReset(SourceResetReason reason) const {
  std::lock_guard lock(callbacks_mutex_);
  if (callbacks_.on_reset) {
    callbacks_.on_reset(reason);
  }
}

void LogSource::emitError(std::string message) const {
  std::lock_guard lock(callbacks_mutex_);
  if (callbacks_.on_error) {
    callbacks_.on_error(std::move(message));
  }
}

void LogSource::updateLinesPerSecond(std::chrono::steady_clock::time_point now,
                                     uint64_t appended_lines) {
  std::lock_guard lock(lines_per_second_mutex_);
  while (!append_rate_samples_.empty() &&
         now - append_rate_samples_.front().timestamp > kLinesPerSecondWindow) {
    append_rate_samples_.pop_front();
  }

  if (appended_lines > 0) {
    append_rate_samples_.push_back(AppendRateSample{
        .timestamp = now,
        .lines = appended_lines,
    });
  }
}

double LogSource::currentLinesPerSecond(std::chrono::steady_clock::time_point now) const {
  std::lock_guard lock(lines_per_second_mutex_);
  if (append_rate_samples_.empty()) {
    return 0.0;
  }

  while (!append_rate_samples_.empty() &&
         now - append_rate_samples_.front().timestamp > kLinesPerSecondWindow) {
    append_rate_samples_.pop_front();
  }

  if (append_rate_samples_.empty()) {
    return 0.0;
  }

  const auto first = append_rate_samples_.front().timestamp;
  const auto elapsed = std::max(
      std::chrono::duration<double>(now - first).count(),
      1.0);

  uint64_t total_lines = 0;
  for (const auto& sample : append_rate_samples_) {
    total_lines += sample.lines;
  }

  return static_cast<double>(total_lines) / elapsed;
}

}  // namespace lgx
