#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <qcorotask.h>

#include "FileMonitor.h"
#include "LogSource.h"

namespace lgx {

/**
 * @brief File-backed log source implementation.
 *
 * FileSource indexes line metadata from a local file, stores source-local page
 * descriptors, and materializes page snapshots through the shared page cache
 * on demand.
 */
class FileSource final : public LogSource {
 public:
  explicit FileSource(std::shared_ptr<IFileMonitor> file_monitor = createFallbackFileMonitor());
  ~FileSource() override;

  [[nodiscard]] std::string path() const override;
  void open(const std::string& path) override;
  void close() override;
  void startIndexing() override;
  void refresh() override;
  void setFollowing(bool enabled) override;
  [[nodiscard]] std::string scannerName() const override;
  [[nodiscard]] std::string requestedScannerName() const override;
  void setRequestedScannerName(std::string name) override;
  [[nodiscard]] SourceSnapshot snapshot() const override;
  [[nodiscard]] uint64_t fileSize() const override;
  void fetchLines(uint64_t first_line, size_t count,
                  std::function<void(SourceLines)> on_ready) override;
  [[nodiscard]] std::optional<SourceLineView> lineViewAt(uint64_t line_number) override;
  void visitLineViews(uint64_t first_line, size_t count,
                      std::function<bool(const SourceLineView&)> visitor) override;
  [[nodiscard]] std::optional<uint64_t> nextLineWithLevel(uint64_t after_line,
                                                          LogLevel level) const override;
  [[nodiscard]] std::optional<uint64_t> previousLineWithLevel(uint64_t before_line,
                                                              LogLevel level) const override;

 private:
  void closeInternal(bool invalidate_pages);
  struct FileIdentity {
    uint64_t device{};
    uint64_t inode{};

    [[nodiscard]] bool operator==(const FileIdentity& other) const noexcept = default;
  };

  struct FileInfo {
    bool exists{false};
    uint64_t size{};
    FileIdentity identity{};
  };

  struct LineIndexEntry {
    uint64_t file_offset{};
    uint32_t length{};
    uint32_t page_index{};
    uint32_t line_index_in_page{};
    LogLevel log_level{LogLevel_Info};
    uint32_t pid{};
    uint32_t tid{};
    int64_t timestamp_msecs_since_epoch{-1};
    TextSpan function_name;
    TextSpan message;
    TextSpan thread_id;
    bool deep_parsed{false};
  };

  [[nodiscard]] static FileInfo statPath(const std::string& path);
  void fail(std::string message);
  void setState(SourceState state);
  void rebuildIndex();
  void invalidateAllPages();
  void resetAndReindex(SourceResetReason reason);
  size_t scanAppendedBytes(uint64_t start_offset);
  void clearIndexedState();
  void appendIndexedLine(uint64_t file_offset, uint32_t logical_length, size_t stored_bytes,
                         FastScanResult scan);
  void extendLastIndexedLine(size_t stored_bytes);
  void startWatching();
  void stopWatching();
  void handleWatchHint(FileEventHint hint);
  [[nodiscard]] PageDataPtr pageSnapshot(size_t page_index) const;
  void ensurePageDeepParsed(size_t page_index);
  [[nodiscard]] SourceLineView buildLineView(size_t line_index, const PageDataPtr& page) const;
  [[nodiscard]] QCoro::Task<PageDataPtr> loadPageSnapshot(size_t page_index) const;
  [[nodiscard]] PageDataPtr readPageSnapshot(size_t page_index) const;
  [[nodiscard]] SourceLines collectLines(uint64_t first_line, size_t count) const;
  [[nodiscard]] std::optional<size_t> pageIndexForLine(size_t line_index) const;

  static constexpr size_t kReadBufferSize = 256 * 1024;
  static constexpr size_t kTargetPageBytes = 64 * 1024;

  std::filesystem::path path_;
  SourceState state_{SourceState::Idle};
  bool following_{false};
  uint64_t indexed_size_{0};
  uint64_t scanned_size_{0};
  uint64_t file_size_{0};
  bool has_identity_{false};
  FileIdentity file_identity_{};
  std::vector<LineIndexEntry> lines_;
  std::string pending_line_bytes_;
  std::optional<uint64_t> pending_line_offset_;
  std::shared_ptr<IFileMonitor> file_monitor_;
  std::unique_ptr<IFileWatch> file_watch_;
  std::unique_ptr<IFileWatch> directory_watch_;
  std::unique_ptr<LogFormatScanner> scanner_;
  std::string requested_scanner_name_{"Auto"};
  bool open_{false};
};

}  // namespace lgx
