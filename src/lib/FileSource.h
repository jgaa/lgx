#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/unordered/unordered_flat_set.hpp>
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
  [[nodiscard]] std::vector<uint32_t> logcatPids() const override;
  [[nodiscard]] std::vector<std::string> systemdProcessNames() const override;
  [[nodiscard]] std::shared_ptr<const SourceWindow> windowForSourceRange(
      uint64_t first_line, size_t count, bool raw) override;
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

  struct LineAnchor {
    size_t first_line{};
    size_t page_index{};
  };

  [[nodiscard]] static FileInfo statPath(const std::string& path);
  void fail(std::string message);
  void setState(SourceState state);
  void rebuildIndex();
  void invalidateAllPages();
  void resetAndReindex(SourceResetReason reason);
  void finalizeIndexedPage(size_t page_index);
  void updateLineAnchorForPage(size_t page_index);
  size_t scanAppendedBytes(uint64_t start_offset);
  void clearIndexedState();
  void updateProcessCatalog(std::string_view logical_line);
  void appendIndexedLine(uint64_t file_offset, uint32_t logical_length, size_t stored_bytes,
                         FastScanResult scan);
  void extendLastIndexedLine(size_t stored_bytes);
  void startWatching();
  void stopWatching();
  void handleWatchHint(FileEventHint hint);
  [[nodiscard]] PageDataPtr pageSnapshot(size_t page_index) const;
  void ensurePageDeepParsed(size_t page_index);
  [[nodiscard]] QCoro::Task<PageDataPtr> loadPageSnapshot(size_t page_index) const;
  [[nodiscard]] PageDataPtr readPageSnapshot(size_t page_index) const;
  [[nodiscard]] std::optional<size_t> pageIndexForLine(size_t line_index) const;

  static constexpr size_t kReadBufferSize = 256 * 1024;
  static constexpr size_t kTargetPageBytes = 64 * 1024;
  static constexpr size_t kLineAnchorStride = 10'000;

  std::filesystem::path path_;
  SourceState state_{SourceState::Idle};
  bool following_{false};
  uint64_t indexed_size_{0};
  uint64_t scanned_size_{0};
  uint64_t file_size_{0};
  bool has_identity_{false};
  FileIdentity file_identity_{};
  std::vector<LineAnchor> line_anchors_;
  std::string pending_line_bytes_;
  std::optional<uint64_t> pending_line_offset_;
  std::shared_ptr<IFileMonitor> file_monitor_;
  std::unique_ptr<IFileWatch> file_watch_;
  std::unique_ptr<IFileWatch> directory_watch_;
  std::unique_ptr<LogFormatScanner> scanner_;
  std::string requested_scanner_name_{"Auto"};
  boost::unordered_flat_set<uint32_t> logcat_pids_;
  boost::unordered_flat_set<std::string> systemd_process_names_;
  std::string previous_systemd_process_name_;
  bool open_{false};
};

}  // namespace lgx
