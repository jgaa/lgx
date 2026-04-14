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
  if (!open_ && path_.empty() && lines() == 0 && pageMetadata().empty()) {
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
    const auto content_replaced =
        !identity_changed && !size_shrank && scanned_size_ > 0 && !matchesIndexedSamples(info.size);
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

    if (content_replaced) {
      resetAndReindex(SourceResetReason::Recreated);
      return;
    }

    if (info.size > scanned_size_) {
      const auto old_line_count = lines();
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
  const bool tailing_empty_source = following_ && file_size_ == 0;
  return SourceSnapshot{
      .state = state_,
      .line_count = lines(),
      .indexed_size = scanned_size_,
      .file_size = fileSize(),
      .following = following_,
      .catching_up = !tailing_empty_source && (state_ == SourceState::Opening
          || state_ == SourceState::Indexing
          || state_ == SourceState::Updating
          || state_ == SourceState::ResetDetected),
      .lines_per_second = linesPerSecond(),
  };
}

uint64_t FileSource::fileSize() const {
  return file_size_;
}

std::vector<uint32_t> FileSource::logcatPids() const {
  std::vector<uint32_t> result;
  result.reserve(logcat_pids_.size());
  for (const auto pid : logcat_pids_) {
    result.push_back(pid);
  }
  return result;
}

std::vector<std::string> FileSource::systemdProcessNames() const {
  std::vector<std::string> result;
  result.reserve(systemd_process_names_.size());
  for (const auto& name : systemd_process_names_) {
    result.push_back(name);
  }
  return result;
}

std::shared_ptr<const SourceWindow> FileSource::windowForSourceRange(uint64_t first_line,
                                                                     size_t count, bool raw) {
  auto window = std::make_shared<SourceWindow>();
  if (count == 0 || first_line >= lines()) {
    return window;
  }

  const auto start_line = static_cast<size_t>(first_line);
  const auto last_requested =
      std::min(lines() - 1U, start_line + count - 1U);
  const auto first_page = pageIndexForLine(start_line);
  const auto last_page = pageIndexForLine(last_requested);
  if (!first_page || !last_page) {
    return window;
  }

  try {
    if (!raw) {
      for (size_t page_index = *first_page; page_index <= *last_page; ++page_index) {
        ensurePageDeepParsed(page_index);
      }
    }

    window->first_source_row_ = pageMetadata()[*first_page].firstLine();
    const auto& last_page_meta = pageMetadata()[*last_page];
    window->last_source_row_ = last_page_meta.firstLine() + last_page_meta.lines();
    window->pages_.reserve(*last_page - *first_page + 1U);
    window->lines_.reserve(window->last_source_row_ - window->first_source_row_);

    for (size_t page_index = *first_page; page_index <= *last_page; ++page_index) {
      const auto page_slot = static_cast<uint32_t>(window->pages_.size());
      const auto page = pageSnapshot(page_index);
      window->pages_.push_back(page);

      const auto& page_meta = pageMetadata()[page_index];
      const auto first_page_line = page_meta.firstLine();
      const auto line_count = page_meta.lines();
      for (size_t offset = 0; offset < line_count; ++offset) {
        const auto source_index = first_page_line + offset;
        const auto& line = page_meta.indexedLine(offset);
        window->lines_.push_back(SourceWindowLine{
            .source_row = static_cast<uint64_t>(source_index),
            .log_level = line.log_level,
            .pid = line.pid,
            .tid = line.tid,
            .function_name = line.function_name,
            .message = line.message,
            .thread_id = line.thread_id,
            .timestamp_msecs_since_epoch = line.timestamp_msecs_since_epoch,
            .page_slot = page_slot,
            .line_index_in_page = static_cast<uint32_t>(offset),
        });
      }
    }
  } catch (const std::exception&) {
    refresh();
    return std::make_shared<SourceWindow>();
  }

  return window;
}

std::optional<uint64_t> FileSource::nextLineWithLevel(uint64_t after_line, LogLevel level) const {
  if (lines() == 0) {
    return std::nullopt;
  }

  const auto start_line = static_cast<size_t>(after_line) + 1U;
  if (start_line >= lines()) {
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
      if (page.indexedLine(line_index - first_line).log_level == level) {
        return static_cast<uint64_t>(line_index);
      }
    }
  }

  return std::nullopt;
}

std::optional<uint64_t> FileSource::previousLineWithLevel(uint64_t before_line,
                                                          LogLevel level) const {
  if (lines() == 0 || before_line == 0) {
    return std::nullopt;
  }

  const auto start_line =
      std::min(static_cast<size_t>(before_line - 1U), lines() - 1U);
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
      if (page.indexedLine(line_index - 1U - first_line).log_level == level) {
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
  updateIndexedSamples();
  setLines(pageMetadata().empty() ? 0 : pageMetadata().back().firstLine() + pageMetadata().back().lines());
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

bool FileSource::matchesIndexedSamples(uint64_t current_size) const {
  if (path_.empty() || scanned_size_ == 0 || current_size < scanned_size_) {
    return false;
  }

  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    return false;
  }

  if (!indexed_prefix_sample_.empty()) {
    std::string prefix(indexed_prefix_sample_.size(), '\0');
    input.seekg(0, std::ios::beg);
    input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    if (static_cast<size_t>(input.gcount()) != prefix.size() || prefix != indexed_prefix_sample_) {
      return false;
    }
  }

  if (!indexed_tail_sample_.empty()) {
    const auto tail_offset = static_cast<std::streamoff>(scanned_size_ - indexed_tail_sample_.size());
    std::string tail(indexed_tail_sample_.size(), '\0');
    input.clear();
    input.seekg(tail_offset, std::ios::beg);
    if (!input) {
      return false;
    }
    input.read(tail.data(), static_cast<std::streamsize>(tail.size()));
    if (static_cast<size_t>(input.gcount()) != tail.size() || tail != indexed_tail_sample_) {
      return false;
    }
  }

  return true;
}

void FileSource::updateIndexedSamples() {
  indexed_prefix_sample_.clear();
  indexed_tail_sample_.clear();

  if (path_.empty() || scanned_size_ == 0) {
    return;
  }

  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    return;
  }

  const auto prefix_bytes = static_cast<size_t>(std::min<uint64_t>(scanned_size_, kConsistencySampleBytes));
  indexed_prefix_sample_.resize(prefix_bytes);
  input.seekg(0, std::ios::beg);
  input.read(indexed_prefix_sample_.data(), static_cast<std::streamsize>(prefix_bytes));
  indexed_prefix_sample_.resize(static_cast<size_t>(input.gcount()));

  const auto tail_bytes = static_cast<size_t>(std::min<uint64_t>(scanned_size_, kConsistencySampleBytes));
  indexed_tail_sample_.resize(tail_bytes);
  input.clear();
  input.seekg(static_cast<std::streamoff>(scanned_size_ - tail_bytes), std::ios::beg);
  if (!input) {
    indexed_tail_sample_.clear();
    return;
  }
  input.read(indexed_tail_sample_.data(), static_cast<std::streamsize>(tail_bytes));
  indexed_tail_sample_.resize(static_cast<size_t>(input.gcount()));
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
  const auto old_line_count = lines();

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
        if (lines() > 0 && !scanner_->startsLogicalLine(logical_line)) {
          extendLastIndexedLine(stored_bytes);
        } else {
          updateProcessCatalog(logical_line);
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
        if (lines() > 0 && !scanner_->startsLogicalLine(logical_line)) {
          extendLastIndexedLine(stored_bytes);
        } else {
          updateProcessCatalog(logical_line);
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

  const auto total_lines =
      pageMetadata().empty() ? 0 : pageMetadata().back().firstLine() + pageMetadata().back().lines();
  setLines(total_lines);
  updateIndexedSamples();
  return total_lines - old_line_count;
}

void FileSource::clearIndexedState() {
  setLines(0);
  indexed_prefix_sample_.clear();
  indexed_tail_sample_.clear();
  line_anchors_.clear();
  logcat_pids_.clear();
  systemd_process_names_.clear();
  previous_systemd_process_name_.clear();
  for (auto& page : pageMetadata()) {
    page.clearLineIndex();
  }
  pageMetadata().clear();
  pending_line_bytes_.clear();
  pending_line_offset_.reset();
}

void FileSource::finalizeIndexedPage(size_t page_index) {
  if (page_index >= pageMetadata().size()) {
    return;
  }
  pageMetadata()[page_index].finalizeIndexedLines();
}

void FileSource::updateLineAnchorForPage(size_t page_index) {
  if (page_index >= pageMetadata().size()) {
    return;
  }

  const auto first_line = pageMetadata()[page_index].firstLine();
  if (line_anchors_.empty()) {
    line_anchors_.push_back(LineAnchor{.first_line = first_line, .page_index = page_index});
    return;
  }

  const auto current_bucket = first_line / kLineAnchorStride;
  const auto last_bucket = line_anchors_.back().first_line / kLineAnchorStride;
  if (current_bucket > last_bucket) {
    line_anchors_.push_back(LineAnchor{.first_line = first_line, .page_index = page_index});
  }
}

void FileSource::updateProcessCatalog(std::string_view logical_line) {
  if (!scanner_) {
    return;
  }

  const auto scanner_name = scanner_->name();
  if (std::string_view(scanner_name) != "Logcat" && std::string_view(scanner_name) != "Systemd") {
    return;
  }

  std::string line_with_newline(logical_line);
  line_with_newline.push_back('\n');
  const auto parsed = scanner_->buildLineIndex(line_with_newline);
  if (parsed.empty()) {
    return;
  }

  const auto& line = parsed.front();
  if (std::string_view(scanner_name) == "Logcat") {
    if (line.pid > 0) {
      logcat_pids_.insert(line.pid);
    }
    return;
  }

  if (!line.function_name.valid()) {
    previous_systemd_process_name_.clear();
    return;
  }

  const auto process_name = sliceSpan(logical_line, line.function_name);
  if (process_name.empty()) {
    previous_systemd_process_name_.clear();
    return;
  }

  if (process_name == previous_systemd_process_name_) {
    return;
  }

  previous_systemd_process_name_.assign(process_name.data(), process_name.size());
  systemd_process_names_.insert(previous_systemd_process_name_);
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
    if (!pageMetadata().empty()) {
      finalizeIndexedPage(pageMetadata().size() - 1U);
    }

    pageMetadata().emplace_back();
    auto& page = pageMetadata().back();
    page.setFirstLine(lines());
    page.setLines(1U);
    page.setFilePos(file_offset);
    page.setSize(stored_bytes);
    page.setOffsetToStartOfLine(0);
    page.setLevelLines(scan.log_level, 1U);
    page.startBuildingIndexedLines();
    if (scan.has_timestamp) {
      page.setTimestampStart(scan.timestamp);
    }
    updateLineAnchorForPage(page_index);
  }

  pageMetadata()[page_index].appendIndexedLine(LogSource::PageMeta::IndexedLine{
      .file_offset = file_offset,
      .length = logical_length,
      .log_level = scan.log_level,
  });
  setLines(lines() + 1U);
}

void FileSource::extendLastIndexedLine(size_t stored_bytes) {
  if (lines() == 0 || pageMetadata().empty() || stored_bytes == 0) {
    return;
  }

  const auto page_index = pageMetadata().size() - 1U;
  auto& page = pageMetadata().back();
  page.extendLastIndexedLine(stored_bytes);
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
    if (!pageMetadata()[page_index].indexedLine(index).deep_parsed) {
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
    auto& line = pageMetadata()[page_index].indexedLine(index);
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
    pageMetadata()[page_index].indexedLine(index).deep_parsed = true;
  }
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

  for (size_t index = 0; index < page_meta.lines(); ++index) {
    const auto& line = page_meta.indexedLine(index);
    offsets.push_back(static_cast<uint32_t>(line.file_offset - static_cast<uint64_t>(offset)));
    levels.push_back(line.log_level);
  }

  return std::make_shared<const PageData>(std::move(storage), bytes_read, std::move(offsets),
                                          std::move(levels));
}

std::optional<size_t> FileSource::pageIndexForLine(size_t line_index) const {
  if (pageMetadata().empty() || line_index >= lines()) {
    return std::nullopt;
  }

  size_t page_index = 0;
  if (!line_anchors_.empty()) {
    const auto upper = std::upper_bound(
        line_anchors_.begin(), line_anchors_.end(), line_index,
        [](size_t value, const LineAnchor& anchor) { return value < anchor.first_line; });
    if (upper != line_anchors_.begin()) {
      page_index = std::prev(upper)->page_index;
    }
  }

  while (page_index + 1U < pageMetadata().size()
         && pageMetadata()[page_index + 1U].firstLine() <= line_index) {
    ++page_index;
  }

  return page_index;
}

}  // namespace lgx
