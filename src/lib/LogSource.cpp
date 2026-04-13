#include "LogSource.h"

#include <algorithm>
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

bool SourceWindow::containsSourceRow(uint64_t source_row) const noexcept {
  return source_row >= first_source_row_ && source_row < last_source_row_;
}

const SourceWindowLine* SourceWindow::lineForSourceRow(uint64_t source_row) const noexcept {
  if (!containsSourceRow(source_row)) {
    return nullptr;
  }

  const auto it = std::lower_bound(
      lines_.cbegin(), lines_.cend(), source_row,
      [](const SourceWindowLine& line, uint64_t row) { return line.source_row < row; });
  if (it == lines_.cend() || it->source_row != source_row) {
    return nullptr;
  }
  return &(*it);
}

std::string_view SourceWindow::rawText(const SourceWindowLine& line) const noexcept {
  if (line.page_slot >= pages_.size() || !pages_[line.page_slot]) {
    return {};
  }
  return pages_[line.page_slot]->lineAt(line.line_index_in_page);
}

std::string_view SourceWindow::functionNameText(const SourceWindowLine& line) const noexcept {
  return sliceSpan(rawText(line), line.function_name);
}

std::string_view SourceWindow::messageText(const SourceWindowLine& line) const noexcept {
  return sliceSpan(rawText(line), line.message);
}

std::string_view SourceWindow::plainText(const SourceWindowLine& line) const noexcept {
  const auto message_text = messageText(line);
  return message_text.empty() ? rawText(line) : message_text;
}

std::string_view SourceWindow::threadIdText(const SourceWindowLine& line) const noexcept {
  return sliceSpan(rawText(line), line.thread_id);
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

void LogSource::PageMeta::startBuildingIndexedLines() {
  indexed_lines_.clear();
  building_indexed_lines_.clear();
}

void LogSource::PageMeta::appendIndexedLine(IndexedLine line) {
  if (!building_indexed_lines_.empty() || indexed_lines_.empty()) {
    building_indexed_lines_.push_back(std::move(line));
    return;
  }

  indexed_lines_.push_back(std::move(line));
}

void LogSource::PageMeta::extendLastIndexedLine(size_t stored_bytes) {
  if (!building_indexed_lines_.empty()) {
    building_indexed_lines_.back().length =
        static_cast<uint32_t>(building_indexed_lines_.back().length + stored_bytes);
    return;
  }

  if (!indexed_lines_.empty()) {
    indexed_lines_.back().length = static_cast<uint32_t>(indexed_lines_.back().length + stored_bytes);
  }
}

void LogSource::PageMeta::finalizeIndexedLines() {
  if (building_indexed_lines_.empty()) {
    if (indexed_lines_.empty()) {
      indexed_lines_.shrink_to_fit();
    }
    return;
  }

  indexed_lines_.assign(building_indexed_lines_.begin(), building_indexed_lines_.end());
  building_indexed_lines_.clear();
}

bool LogSource::PageMeta::isBuildingIndexedLines() const noexcept {
  return !building_indexed_lines_.empty();
}

const LogSource::PageMeta::IndexedLine& LogSource::PageMeta::indexedLine(size_t index) const {
  if (!building_indexed_lines_.empty()) {
    return building_indexed_lines_.at(index);
  }
  return indexed_lines_.at(index);
}

LogSource::PageMeta::IndexedLine& LogSource::PageMeta::indexedLine(size_t index) {
  if (!building_indexed_lines_.empty()) {
    return building_indexed_lines_.at(index);
  }
  return indexed_lines_.at(index);
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

std::vector<uint32_t> LogSource::logcatPids() const {
  return {};
}

std::vector<std::string> LogSource::systemdProcessNames() const {
  return {};
}

std::shared_ptr<const SourceWindow> LogSource::windowForSourceRange(uint64_t first_line,
                                                                    size_t, bool) {
  auto window = std::make_shared<SourceWindow>();
  window->first_source_row_ = first_line;
  window->last_source_row_ = first_line;
  return window;
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
