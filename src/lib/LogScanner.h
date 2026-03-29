#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <QStringList>

#include "LogTypes.h"

namespace lgx {

struct ParsedSpan {
  uint32_t start{};
  uint32_t length{};

  [[nodiscard]] bool valid() const noexcept { return length > 0; }
};

struct ParsedLineMetadata {
  uint32_t line_offset{};
  uint32_t line_length{};
  LogLevel log_level{LogLevel_Info};
  ParsedSpan thread_id;
  ParsedSpan function_name;
  ParsedSpan message;
  bool has_timestamp{false};
  int64_t timestamp_msecs_since_epoch{};
};

struct FastScanResult {
  LogLevel log_level{LogLevel_Info};
  bool has_timestamp{false};
  std::chrono::system_clock::time_point timestamp{};
};

class LogFormatScanner {
 public:
  virtual ~LogFormatScanner() = default;

  [[nodiscard]] virtual const char* name() const noexcept = 0;
  [[nodiscard]] virtual FastScanResult scanLineFast(std::string_view line) const noexcept = 0;
  [[nodiscard]] virtual std::vector<ParsedLineMetadata> buildLineIndex(
      std::string_view page_bytes) const = 0;
};

std::unique_ptr<LogFormatScanner> createDefaultLogScanner();
std::unique_ptr<LogFormatScanner> createLogScannerByName(std::string_view requested_name);
[[nodiscard]] QStringList availableLogScannerNames();

}  // namespace lgx
