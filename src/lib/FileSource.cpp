#include "FileSource.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <QFileInfo>

#include <qcorotask.h>

#ifdef __unix__
#include <sys/stat.h>
#endif

namespace lgx {
namespace {

size_t levelIndex(LogLevel level) noexcept {
  return static_cast<size_t>(std::clamp(static_cast<int>(level), static_cast<int>(LogLevel_Error),
                                        static_cast<int>(LogLevel_Trace)));
}

std::string sysErrorMessage(const std::string& path) {
  return path + ": " + std::strerror(errno);
}

std::string_view sliceSpan(std::string_view text, ParsedSpan span) noexcept {
  if (!span.valid()) {
    return {};
  }

  const auto start = static_cast<size_t>(span.start);
  const auto length = static_cast<size_t>(span.length);
  if (start >= text.size()) {
    return {};
  }

  return text.substr(start, std::min(length, text.size() - start));
}

TextSpan toTextSpan(ParsedSpan span) noexcept {
  if (!span.valid()) {
    return {};
  }

  return TextSpan{
      .offset = static_cast<int32_t>(span.start),
      .length = span.length,
  };
}

}  // namespace

FileSource::FileSource(std::shared_ptr<IFileMonitor> file_monitor)
    : file_monitor_(std::move(file_monitor)),
      scanner_(createDefaultLogScanner()) {}

FileSource::~FileSource() {
  closeInternal(false);
}

std::string FileSource::path() const {
  return path_.string();
}

void FileSource::open(const std::string& path) {
  close();

  path_ = std::filesystem::path(path);
  setState(SourceState::Opening);

  const auto info = statPath(path_.string());
  if (!info.exists) {
    fail("File does not exist: " + path_.string());
    return;
  }

  file_size_ = info.size;
  indexed_size_ = 0;
  scanned_size_ = 0;
  file_identity_ = info.identity;
  has_identity_ = true;
  open_ = true;
  startWatching();
  setState(SourceState::Idle);
}

void FileSource::close() {
  closeInternal(true);
}

void FileSource::closeInternal(bool invalidate_pages) {
  if (!open_ && path_.empty() && lines_.empty() && pageMetadata().empty()) {
    return;
  }

  open_ = false;
  stopWatching();
  if (invalidate_pages) {
    invalidateAllPages();
  }

  path_.clear();
  state_ = SourceState::Idle;
  following_ = false;
  indexed_size_ = 0;
  scanned_size_ = 0;
  file_size_ = 0;
  has_identity_ = false;
  file_identity_ = {};
  clearIndexedState();
  setLines(0);
  updateLinesPerSecond(std::chrono::steady_clock::now(), 0);
}

void FileSource::startIndexing() {
  if (path_.empty()) {
    fail("Cannot index without an opened file path");
    return;
  }

  try {
    setState(SourceState::Indexing);
    rebuildIndex();
    setState(SourceState::Ready);
  } catch (const std::exception& ex) {
    fail(ex.what());
  }
}

void FileSource::refresh() {
  if (path_.empty()) {
    return;
  }

  try {
    setState(SourceState::Updating);

    const auto info = statPath(path_.string());
    if (!info.exists) {
      invalidateAllPages();
      clearIndexedState();
      file_size_ = 0;
      indexed_size_ = 0;
      scanned_size_ = 0;
      has_identity_ = false;
      file_identity_ = {};
      setLines(0);
      setState(SourceState::ResetDetected);
      emitReset(SourceResetReason::Disappeared);
      if (!path_.empty()) {
        startWatching();
      }
      return;
    }

    const auto identity_changed = !has_identity_ || !(info.identity == file_identity_);
    const auto size_shrank = has_identity_ && info.size < scanned_size_;
    file_identity_ = info.identity;
    has_identity_ = true;
    file_size_ = info.size;

    if (identity_changed) {
      resetAndReindex(SourceResetReason::Recreated);
      return;
    }

    if (size_shrank) {
      resetAndReindex(SourceResetReason::Truncated);
      return;
    }

    if (info.size > scanned_size_) {
      const auto old_line_count = lines_.size();
      const auto appended_lines = scanAppendedBytes(scanned_size_);
      indexed_size_ = scanned_size_;
      updateLinesPerSecond(std::chrono::steady_clock::now(), appended_lines);
      setState(SourceState::Ready);
      if (appended_lines > 0) {
        emitLinesAppended(old_line_count, appended_lines);
      }
      return;
    }

    setState(SourceState::Ready);
  } catch (const std::exception& ex) {
    fail(ex.what());
  }
}

void FileSource::setFollowing(bool enabled) {
  following_ = enabled;
  if (path_.empty()) {
    return;
  }

  startWatching();
}

std::string FileSource::scannerName() const {
  return scanner_ ? scanner_->name() : std::string{};
}

std::string FileSource::requestedScannerName() const {
  return requested_scanner_name_;
}

void FileSource::setRequestedScannerName(std::string name) {
  if (name.empty()) {
    name = "Auto";
  }

  if (requested_scanner_name_ == name && scanner_) {
    return;
  }

  requested_scanner_name_ = std::move(name);
  scanner_ = createLogScannerByName(requested_scanner_name_);

  if (path_.empty()) {
    return;
  }

  resetAndReindex(SourceResetReason::Recreated);
}

SourceSnapshot FileSource::snapshot() const {
  return SourceSnapshot{
      .state = state_,
      .line_count = lines_.size(),
      .indexed_size = scanned_size_,
      .file_size = fileSize(),
      .following = following_,
      .catching_up = state_ == SourceState::Opening
          || state_ == SourceState::Indexing
          || state_ == SourceState::Updating
          || state_ == SourceState::ResetDetected,
      .lines_per_second = linesPerSecond(),
  };
}

uint64_t FileSource::fileSize() const {
  return file_size_;
}

void FileSource::fetchLines(uint64_t first_line, size_t count,
                            std::function<void(SourceLines)> on_ready) {
  if (!on_ready) {
    return;
  }

  try {
    on_ready(collectLines(first_line, count));
  } catch (const std::exception& ex) {
    emitError(ex.what());
    on_ready({});
  }
}

std::optional<SourceLineView> FileSource::lineViewAt(uint64_t line_number) {
  if (line_number >= lines_.size()) {
    return std::nullopt;
  }

  const auto page_index = static_cast<size_t>(lines_[static_cast<size_t>(line_number)].page_index);
  ensurePageDeepParsed(page_index);
  const auto page = pageSnapshot(page_index);
  return buildLineView(static_cast<size_t>(line_number), page);
}

void FileSource::visitLineViews(uint64_t first_line, size_t count,
                                std::function<bool(const SourceLineView&)> visitor) {
  if (!visitor || count == 0 || first_line >= lines_.size()) {
    return;
  }

  const auto available = lines_.size() - static_cast<size_t>(first_line);
  const auto actual_count = std::min(count, available);
  size_t current_page_index = std::numeric_limits<size_t>::max();
  PageDataPtr current_page;

  for (size_t offset = 0; offset < actual_count; ++offset) {
    const auto line_index = static_cast<size_t>(first_line) + offset;
    const auto page_index = static_cast<size_t>(lines_[line_index].page_index);
    if (page_index != current_page_index) {
      ensurePageDeepParsed(page_index);
      current_page = pageSnapshot(page_index);
      current_page_index = page_index;
    }

    if (!visitor(buildLineView(line_index, current_page))) {
      break;
    }
  }
}

std::optional<uint64_t> FileSource::nextLineWithLevel(uint64_t after_line, LogLevel level) const {
  if (lines_.empty()) {
    return std::nullopt;
  }

  const auto start_line = static_cast<size_t>(after_line) + 1U;
  if (start_line >= lines_.size()) {
    return std::nullopt;
  }

  const auto start_page = pageIndexForLine(start_line);
  if (!start_page) {
    return std::nullopt;
  }

  for (size_t page_index = *start_page; page_index < pageMetadata().size(); ++page_index) {
    const auto& page = pageMetadata()[page_index];
    if (page.levelLines(level) == 0) {
      continue;
    }

    const auto first_line = page.firstLine();
    const auto last_line = first_line + page.lines();
    const auto search_start = std::max(start_line, first_line);
    for (size_t line_index = search_start; line_index < last_line; ++line_index) {
      if (lines_[line_index].log_level == level) {
        return static_cast<uint64_t>(line_index);
      }
    }
  }

  return std::nullopt;
}

std::optional<uint64_t> FileSource::previousLineWithLevel(uint64_t before_line,
                                                          LogLevel level) const {
  if (lines_.empty() || before_line == 0) {
    return std::nullopt;
  }

  const auto start_line =
      std::min(static_cast<size_t>(before_line - 1U), lines_.size() - 1U);
  const auto start_page = pageIndexForLine(start_line);
  if (!start_page) {
    return std::nullopt;
  }

  for (size_t page_index = *start_page + 1U; page_index > 0; --page_index) {
    const auto& page = pageMetadata()[page_index - 1U];
    if (page.levelLines(level) == 0) {
      continue;
    }

    const auto first_line = page.firstLine();
    const auto last_line = first_line + page.lines();
    const auto search_end = std::min(start_line + 1U, last_line);
    for (size_t line_index = search_end; line_index > first_line; --line_index) {
      if (lines_[line_index - 1U].log_level == level) {
        return static_cast<uint64_t>(line_index - 1U);
      }
    }
  }

  return std::nullopt;
}

FileSource::FileInfo FileSource::statPath(const std::string& path) {
  FileInfo info;

#ifdef __unix__
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) {
      return info;
    }

    throw std::runtime_error(sysErrorMessage(path));
  }

  info.exists = true;
  info.size = static_cast<uint64_t>(st.st_size);
  info.identity.device = static_cast<uint64_t>(st.st_dev);
  info.identity.inode = static_cast<uint64_t>(st.st_ino);
  return info;
#else
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    if (ec) {
      throw std::runtime_error(path + ": " + ec.message());
    }
    return info;
  }

  info.exists = true;
  info.size = std::filesystem::file_size(path, ec);
  if (ec) {
    throw std::runtime_error(path + ": " + ec.message());
  }
  info.identity.device = std::hash<std::string>{}(std::filesystem::absolute(path).string());
  info.identity.inode = info.size;
  return info;
#endif
}

void FileSource::fail(std::string message) {
  state_ = SourceState::Failed;
  emitError(std::move(message));
  emitStateChanged(snapshot());
}

void FileSource::setState(SourceState state) {
  state_ = state;
  emitStateChanged(snapshot());
}

void FileSource::rebuildIndex() {
  invalidateAllPages();
  clearIndexedState();
  updateLinesPerSecond(std::chrono::steady_clock::now(), 0);

  const auto info = statPath(path_.string());
  if (!info.exists) {
    throw std::runtime_error("File disappeared while indexing: " + path_.string());
  }
  file_size_ = info.size;
  file_identity_ = info.identity;
  has_identity_ = true;
  scanAppendedBytes(0);
  indexed_size_ = scanned_size_;
  setLines(lines_.size());
}

void FileSource::invalidateAllPages() {
  for (size_t page_index = 0; page_index < pageMetadata().size(); ++page_index) {
    sharedPageCache().invalidate(PageKey{sourceId(), page_index});
  }
}

void FileSource::resetAndReindex(SourceResetReason reason) {
  invalidateAllPages();
  clearIndexedState();
  indexed_size_ = 0;
  scanned_size_ = 0;
  setLines(0);
  setState(SourceState::ResetDetected);
  emitReset(reason);
  rebuildIndex();
  setState(SourceState::Ready);
  if (!path_.empty()) {
    startWatching();
  }
}

size_t FileSource::scanAppendedBytes(uint64_t start_offset) {
  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open " + path_.string() + " for reading");
  }

  input.seekg(static_cast<std::streamoff>(start_offset), std::ios::beg);
  if (!input) {
    throw std::runtime_error("Failed to seek " + path_.string() + " for reading");
  }

  std::vector<char> buffer(kReadBufferSize);
  pending_line_bytes_.reserve(std::max<size_t>(pending_line_bytes_.capacity(), 4096));
  auto carryover = pending_line_bytes_;
  auto carry_start_offset = pending_line_offset_.value_or(start_offset);
  auto chunk_start_offset = start_offset;
  const auto old_line_count = lines_.size();

  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto bytes_read = static_cast<size_t>(input.gcount());
    if (bytes_read == 0) {
      break;
    }

    std::string_view chunk(buffer.data(), bytes_read);
    size_t line_start = 0;
    for (size_t index = 0; index < chunk.size(); ++index) {
      if (chunk[index] != '\n') {
        continue;
      }

      const auto line_end = index;
      if (!carryover.empty()) {
        carryover.append(chunk.data() + line_start, line_end - line_start);
        std::string_view logical_line(carryover);
        if (!logical_line.empty() && logical_line.back() == '\r') {
          logical_line.remove_suffix(1);
        }

        const auto stored_bytes = carryover.size() + 1U;
        if (!lines_.empty() && !scanner_->startsLogicalLine(logical_line)) {
          extendLastIndexedLine(stored_bytes);
        } else {
          appendIndexedLine(carry_start_offset, logical_line.size(), stored_bytes,
                            scanner_->scanLineFast(logical_line));
        }
        carryover.clear();
        pending_line_offset_.reset();
      } else {
        auto logical_line = chunk.substr(line_start, line_end - line_start);
        if (!logical_line.empty() && logical_line.back() == '\r') {
          logical_line.remove_suffix(1);
        }

        const auto file_offset = chunk_start_offset + line_start;
        const auto stored_bytes = (line_end - line_start) + 1U;
        if (!lines_.empty() && !scanner_->startsLogicalLine(logical_line)) {
          extendLastIndexedLine(stored_bytes);
        } else {
          appendIndexedLine(file_offset, logical_line.size(), stored_bytes,
                            scanner_->scanLineFast(logical_line));
        }
      }

      line_start = index + 1U;
    }

    if (line_start < chunk.size()) {
      if (carryover.empty()) {
        carry_start_offset = chunk_start_offset + line_start;
        pending_line_offset_ = carry_start_offset;
      }
      carryover.append(chunk.data() + line_start, chunk.size() - line_start);
    }

    chunk_start_offset += bytes_read;
  }

  scanned_size_ = chunk_start_offset;
  pending_line_bytes_ = std::move(carryover);
  if (pending_line_bytes_.empty()) {
    pending_line_offset_.reset();
  } else if (!pending_line_offset_) {
    pending_line_offset_ = carry_start_offset;
  }

  setLines(lines_.size());
  return lines_.size() - old_line_count;
}

void FileSource::clearIndexedState() {
  lines_.clear();
  for (auto& page : pageMetadata()) {
    page.clearLineIndex();
  }
  pageMetadata().clear();
  pending_line_bytes_.clear();
  pending_line_offset_.reset();
}

void FileSource::appendIndexedLine(uint64_t file_offset, uint32_t logical_length, size_t stored_bytes,
                                   FastScanResult scan) {
  size_t page_index = pageMetadata().size();
  size_t line_index_in_page = 0;

  if (!pageMetadata().empty()) {
    auto& last_page = pageMetadata().back();
    if (last_page.size() + stored_bytes <= kTargetPageBytes) {
      page_index = pageMetadata().size() - 1U;
      line_index_in_page = last_page.lines();
      last_page.setLines(last_page.lines() + 1U);
      last_page.setSize(last_page.size() + stored_bytes);
      last_page.setLevelLines(scan.log_level, last_page.levelLines(scan.log_level) + 1U);
      last_page.clearLineIndex();
      sharedPageCache().invalidate(PageKey{sourceId(), page_index});
    }
  }

  if (page_index == pageMetadata().size()) {
    pageMetadata().emplace_back();
    auto& page = pageMetadata().back();
    page.setFirstLine(lines_.size());
    page.setLines(1U);
    page.setFilePos(file_offset);
    page.setSize(stored_bytes);
    page.setOffsetToStartOfLine(0);
    page.setLevelLines(scan.log_level, 1U);
    if (scan.has_timestamp) {
      page.setTimestampStart(scan.timestamp);
    }
  }

  lines_.push_back(LineIndexEntry{
      .file_offset = file_offset,
      .length = logical_length,
      .page_index = static_cast<uint32_t>(page_index),
      .line_index_in_page = static_cast<uint32_t>(line_index_in_page),
      .log_level = scan.log_level,
  });
}

void FileSource::extendLastIndexedLine(size_t stored_bytes) {
  if (lines_.empty() || pageMetadata().empty() || stored_bytes == 0) {
    return;
  }

  auto& line = lines_.back();
  line.length = static_cast<uint32_t>(line.length + stored_bytes);

  const auto page_index = pageMetadata().size() - 1U;
  auto& page = pageMetadata().back();
  page.setSize(page.size() + stored_bytes);
  page.clearLineIndex();
  sharedPageCache().invalidate(PageKey{sourceId(), page_index});
}

void FileSource::startWatching() {
  stopWatching();

  if (!file_monitor_ || path_.empty()) {
    return;
  }

  const auto path_string = path_.string();
  file_watch_ = file_monitor_->watchFile(path_string, [this](FileEventHint hint) {
    handleWatchHint(hint);
  });

  const QFileInfo info(QString::fromStdString(path_string));
  const auto directory = info.absolutePath();
  if (!directory.isEmpty()) {
    directory_watch_ = file_monitor_->watchDirectory(
        directory.toStdString(), [this](FileEventHint hint) { handleWatchHint(hint); });
  }
}

void FileSource::stopWatching() {
  file_watch_.reset();
  directory_watch_.reset();
}

void FileSource::handleWatchHint(FileEventHint) {
  if (path_.empty()) {
    return;
  }

  refresh();
}

PageDataPtr FileSource::pageSnapshot(size_t page_index) const {
  return QCoro::waitFor(loadPageViaCache(
      page_index, [this, page_index]() { return loadPageSnapshot(page_index); }));
}

void FileSource::ensurePageDeepParsed(size_t page_index) {
  if (page_index >= pageMetadata().size()) {
    return;
  }

  const auto first_line = pageMetadata()[page_index].firstLine();
  const auto line_count = pageMetadata()[page_index].lines();
  bool needs_parse = false;
  for (size_t index = 0; index < line_count; ++index) {
    if (!lines_[first_line + index].deep_parsed) {
      needs_parse = true;
      break;
    }
  }
  if (!needs_parse) {
    return;
  }

  const auto page = pageSnapshot(page_index);
  const auto& page_meta = pageMetadata()[page_index];
  const auto& line_index = page_meta.ensureLineIndex([this, page]() {
    auto built = std::make_unique<PageMeta::LineIndex>();
    const auto bytes = page->bytes();
    built->lines = scanner_->buildLineIndex(std::string_view(bytes.data(), bytes.size()));
    return built;
  });

  const auto parsed_count = std::min(line_count, line_index.lines.size());
  for (size_t index = 0; index < parsed_count; ++index) {
    auto& line = lines_[first_line + index];
    if (line.deep_parsed) {
      continue;
    }

    const auto& parsed = line_index.lines[index];
    line.log_level = parsed.log_level;
    line.pid = parsed.pid;
    line.tid = parsed.tid;
    line.function_name = toTextSpan(parsed.function_name);
    line.message = toTextSpan(parsed.message);
    line.thread_id = toTextSpan(parsed.thread_id);
    line.timestamp_msecs_since_epoch = parsed.has_timestamp ? parsed.timestamp_msecs_since_epoch : -1;
    line.deep_parsed = true;
  }

  for (size_t index = parsed_count; index < line_count; ++index) {
    lines_[first_line + index].deep_parsed = true;
  }
}

SourceLineView FileSource::buildLineView(size_t line_index, const PageDataPtr& page) const {
  const auto& line = lines_[line_index];
  SourceLineView view;
  view.line_number = line_index;
  view.log_level = line.log_level;
  view.pid = line.pid;
  view.tid = line.tid;
  view.function_name = line.function_name;
  view.message = line.message;
  view.thread_id = line.thread_id;
  view.timestamp_msecs_since_epoch = line.timestamp_msecs_since_epoch;
  view.page = page;
  view.line_index_in_page = line.line_index_in_page;
  return view;
}

QCoro::Task<PageDataPtr> FileSource::loadPageSnapshot(size_t page_index) const {
  co_return readPageSnapshot(page_index);
}

PageDataPtr FileSource::readPageSnapshot(size_t page_index) const {
  if (page_index >= pageMetadata().size()) {
    throw std::out_of_range("Requested page index is out of range");
  }

  const auto& page_meta = pageMetadata()[page_index];
  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to reopen " + path_.string() + " for page load");
  }

  const auto offset = static_cast<std::streamoff>(page_meta.filePos());
  input.seekg(offset, std::ios::beg);
  if (!input) {
    throw std::runtime_error("Failed to seek log file for page load");
  }

  auto storage = std::make_shared<std::vector<char>>(page_meta.size());
  input.read(storage->data(), static_cast<std::streamsize>(storage->size()));
  const auto bytes_read = static_cast<size_t>(input.gcount());
  if (bytes_read != storage->size()) {
    throw std::runtime_error("Short read while loading log page");
  }

  std::vector<uint32_t> offsets;
  std::vector<LogLevel> levels;
  offsets.reserve(page_meta.lines());
  levels.reserve(page_meta.lines());

  const auto first_line = page_meta.firstLine();
  for (size_t index = 0; index < page_meta.lines(); ++index) {
    const auto& line = lines_[first_line + index];
    offsets.push_back(static_cast<uint32_t>(line.file_offset - static_cast<uint64_t>(offset)));
    levels.push_back(line.log_level);
  }

  return std::make_shared<const PageData>(std::move(storage), bytes_read, std::move(offsets),
                                          std::move(levels));
}

SourceLines FileSource::collectLines(uint64_t first_line, size_t count) const {
  SourceLines result;
  result.first_line = first_line;

  if (first_line >= lines_.size() || count == 0) {
    return result;
  }

  const auto remaining = lines_.size() - static_cast<size_t>(first_line);
  const auto actual_count = std::min(count, remaining);
  result.lines.reserve(actual_count);

  std::vector<PageDataPtr> loaded_pages;
  std::vector<size_t> loaded_page_indices;

  for (size_t i = 0; i < actual_count; ++i) {
    const auto& line = lines_[first_line + i];
    const auto page_index = static_cast<size_t>(line.page_index);

    size_t page_slot = loaded_pages.size();
    const auto existing = std::find(loaded_page_indices.begin(), loaded_page_indices.end(), page_index);
    if (existing != loaded_page_indices.end()) {
      page_slot = static_cast<size_t>(std::distance(loaded_page_indices.begin(), existing));
    } else {
      loaded_page_indices.push_back(page_index);
      loaded_pages.push_back(QCoro::waitFor(loadPageViaCache(
          page_index, [this, page_index]() { return loadPageSnapshot(page_index); })));
    }

    const auto& page = loaded_pages[page_slot];
    const auto& page_meta = pageMetadata()[page_index];
    const auto& line_index = page_meta.ensureLineIndex([this, page]() {
      auto built = std::make_unique<PageMeta::LineIndex>();
      const auto bytes = page->bytes();
      built->lines = scanner_->buildLineIndex(std::string_view(bytes.data(), bytes.size()));
      return built;
    });
    const auto page_local_index = static_cast<size_t>(line.line_index_in_page);
    const auto raw_text = std::string(page->lineAt(page_local_index));

    SourceLine source_line{
        .line_number = first_line + i,
        .log_level = line.log_level,
        .pid = 0,
        .tid = 0,
        .text = raw_text,
        .message = raw_text,
    };

    if (page_local_index < line_index.lines.size()) {
      const auto& parsed = line_index.lines[page_local_index];
      source_line.log_level = parsed.log_level;
      source_line.pid = parsed.pid;
      source_line.tid = parsed.tid;
      source_line.thread_id = std::string(sliceSpan(page->lineAt(page_local_index), parsed.thread_id));
      source_line.function_name =
          std::string(sliceSpan(page->lineAt(page_local_index), parsed.function_name));
      source_line.message = std::string(sliceSpan(page->lineAt(page_local_index), parsed.message));
      if (parsed.has_timestamp) {
        source_line.timestamp =
            std::chrono::system_clock::time_point{std::chrono::milliseconds{
                parsed.timestamp_msecs_since_epoch}};
      }
    }

    result.lines.push_back(std::move(source_line));
  }

  return result;
}

std::optional<size_t> FileSource::pageIndexForLine(size_t line_index) const {
  if (pageMetadata().empty() || line_index >= lines_.size()) {
    return std::nullopt;
  }

  const auto upper = std::upper_bound(
      pageMetadata().begin(), pageMetadata().end(), line_index,
      [](size_t value, const PageMeta& page) { return value < page.firstLine(); });
  if (upper == pageMetadata().begin()) {
    return 0;
  }

  return static_cast<size_t>(std::distance(pageMetadata().begin(), std::prev(upper)));
}

}  // namespace lgx
