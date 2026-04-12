#include "LogSource.h"

#include <cassert>

namespace lgx {

namespace {

std::string_view sliceSpan(std::string_view text, TextSpan span) noexcept {
  if (!span.isSet()) {
    return {};
  }

  const auto start = static_cast<size_t>(span.offset);
  const auto length = static_cast<size_t>(span.length);
  if (start >= text.size()) {
    return {};
  }

  return text.substr(start, std::min(length, text.size() - start));
}

}  // namespace

std::string_view SourceLineView::rawText() const noexcept {
  if (page) {
    return page->lineAt(line_index_in_page);
  }
  if (owned_raw_text) {
    return *owned_raw_text;
  }
  return {};
}

std::string_view SourceLineView::functionNameText() const noexcept {
  if (page) {
    return sliceSpan(rawText(), function_name);
  }
  if (owned_function_name) {
    return *owned_function_name;
  }
  return {};
}

std::string_view SourceLineView::messageText() const noexcept {
  if (page) {
    return sliceSpan(rawText(), message);
  }
  if (owned_message) {
    return *owned_message;
  }
  return {};
}

std::string_view SourceLineView::plainText() const noexcept {
  const auto message_text = messageText();
  return message_text.empty() ? rawText() : message_text;
}

std::string_view SourceLineView::threadIdText() const noexcept {
  if (page) {
    return sliceSpan(rawText(), thread_id);
  }
  if (owned_thread_id) {
    return *owned_thread_id;
  }
  return {};
}

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

std::optional<SourceLineView> LogSource::lineViewAt(uint64_t line_number) {
  std::optional<SourceLineView> result;
  fetchLines(line_number, 1, [&result](SourceLines lines) {
    if (lines.lines.empty()) {
      return;
    }

    const auto& line = lines.lines.front();
    SourceLineView view;
    view.line_number = line.line_number;
    view.log_level = line.log_level;
    view.pid = line.pid;
    view.tid = line.tid;
    view.owned_raw_text = std::make_shared<std::string>(line.text);
    if (!line.function_name.empty()) {
      view.owned_function_name = std::make_shared<std::string>(line.function_name);
    }
    if (!line.message.empty()) {
      view.owned_message = std::make_shared<std::string>(line.message);
    }
    if (!line.thread_id.empty()) {
      view.owned_thread_id = std::make_shared<std::string>(line.thread_id);
    }
    if (line.timestamp.has_value()) {
      view.timestamp_msecs_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
          line.timestamp->time_since_epoch()).count();
    }
    result = std::move(view);
  });
  return result;
}

void LogSource::visitLineViews(uint64_t first_line, size_t count,
                               std::function<bool(const SourceLineView&)> visitor) {
  if (!visitor || count == 0) {
    return;
  }

  for (size_t index = 0; index < count; ++index) {
    const auto view = lineViewAt(first_line + index);
    if (!view.has_value()) {
      continue;
    }
    if (!visitor(*view)) {
      break;
    }
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
