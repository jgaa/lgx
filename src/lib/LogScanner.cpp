#include "LogScanner.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace lgx {
namespace {

using namespace std::literals;

[[nodiscard]] bool isDigit(char ch) noexcept {
  return ch >= '0' && ch <= '9';
}

[[nodiscard]] bool parseFixedDigits(std::string_view text, size_t offset, size_t digits,
                                    int& value) noexcept {
  if (offset + digits > text.size()) {
    return false;
  }

  int parsed = 0;
  for (size_t index = 0; index < digits; ++index) {
    const char ch = text[offset + index];
    if (!isDigit(ch)) {
      return false;
    }
    parsed = parsed * 10 + (ch - '0');
  }

  value = parsed;
  return true;
}

[[nodiscard]] std::optional<int64_t> parseTimestampMillis(std::string_view line) noexcept {
  if (line.size() < 23 || line[4] != '-' || line[7] != '-' || line[10] != ' ' || line[13] != ':' ||
      line[16] != ':' || line[19] != '.') {
    return std::nullopt;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millis = 0;
  if (!parseFixedDigits(line, 0, 4, year) || !parseFixedDigits(line, 5, 2, month) ||
      !parseFixedDigits(line, 8, 2, day) || !parseFixedDigits(line, 11, 2, hour) ||
      !parseFixedDigits(line, 14, 2, minute) || !parseFixedDigits(line, 17, 2, second) ||
      !parseFixedDigits(line, 20, 3, millis)) {
    return std::nullopt;
  }

  const QDate date(year, month, day);
  const QTime time(hour, minute, second, millis);
  if (!date.isValid() || !time.isValid()) {
    return std::nullopt;
  }

  const QDateTime date_time(date, time, QTimeZone::systemTimeZone());
  if (!date_time.isValid()) {
    return std::nullopt;
  }

  return date_time.toMSecsSinceEpoch();
}

[[nodiscard]] std::optional<std::pair<size_t, size_t>> nextToken(std::string_view text,
                                                                 size_t start) noexcept {
  while (start < text.size() && text[start] == ' ') {
    ++start;
  }
  if (start >= text.size()) {
    return std::nullopt;
  }

  size_t end = start;
  while (end < text.size() && text[end] != ' ') {
    ++end;
  }
  return std::make_pair(start, end);
}

[[nodiscard]] LogLevel parseLevelToken(std::string_view token) noexcept {
  if (token == "ERROR"sv) {
    return LogLevel_Error;
  }
  if (token == "WARNING"sv || token == "WARN"sv) {
    return LogLevel_Warn;
  }
  if (token == "NOTICE"sv) {
    return LogLevel_Notice;
  }
  if (token == "DEBUG"sv) {
    return LogLevel_Debug;
  }
  if (token == "TRACE"sv) {
    return LogLevel_Trace;
  }
  return LogLevel_Info;
}

[[nodiscard]] ParsedLineMetadata parseLogfaultLine(std::string_view line,
                                                   uint32_t line_offset) noexcept {
  ParsedLineMetadata parsed;
  parsed.line_offset = line_offset;
  parsed.line_length = static_cast<uint32_t>(line.size());
  parsed.log_level = LogLevel_Info;
  parsed.message = ParsedSpan{0, static_cast<uint32_t>(line.size())};

  const auto timestamp_msecs = parseTimestampMillis(line);
  if (!timestamp_msecs) {
    return parsed;
  }

  parsed.has_timestamp = true;
  parsed.timestamp_msecs_since_epoch = *timestamp_msecs;

  auto token = nextToken(line, 24);
  if (!token) {
    return parsed;
  }

  token = nextToken(line, token->second);
  if (!token) {
    return parsed;
  }
  parsed.log_level = parseLevelToken(line.substr(token->first, token->second - token->first));

  token = nextToken(line, token->second);
  if (!token) {
    return parsed;
  }
  parsed.thread_id = ParsedSpan{static_cast<uint32_t>(token->first),
                                static_cast<uint32_t>(token->second - token->first)};

  size_t rest_start = token->second;
  while (rest_start < line.size() && line[rest_start] == ' ') {
    ++rest_start;
  }

  parsed.message = ParsedSpan{static_cast<uint32_t>(rest_start),
                              static_cast<uint32_t>(line.size() - rest_start)};

  if (rest_start < line.size() && line[rest_start] == '{') {
    const auto closing = line.find('}', rest_start + 1U);
    if (closing != std::string_view::npos && closing + 1U <= line.size()) {
      parsed.function_name =
          ParsedSpan{static_cast<uint32_t>(rest_start + 1U),
                     static_cast<uint32_t>(closing - (rest_start + 1U))};

      size_t message_start = closing + 1U;
      while (message_start < line.size() && line[message_start] == ' ') {
        ++message_start;
      }

      parsed.message = ParsedSpan{static_cast<uint32_t>(message_start),
                                  static_cast<uint32_t>(line.size() - message_start)};
    }
  }

  return parsed;
}

class LogfaultScanner final : public LogFormatScanner {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "Logfault"; }

  [[nodiscard]] FastScanResult scanLineFast(std::string_view line) const noexcept override {
    const auto parsed = parseLogfaultLine(line, 0);
    FastScanResult result;
    result.log_level = parsed.log_level;
    result.has_timestamp = parsed.has_timestamp;
    if (parsed.has_timestamp) {
      result.timestamp =
          std::chrono::system_clock::time_point{std::chrono::milliseconds{
              parsed.timestamp_msecs_since_epoch}};
    }
    return result;
  }

  [[nodiscard]] std::vector<ParsedLineMetadata> buildLineIndex(
      std::string_view page_bytes) const override {
    std::vector<ParsedLineMetadata> lines;
    if (page_bytes.empty()) {
      return lines;
    }

    size_t line_start = 0;
    for (size_t index = 0; index < page_bytes.size(); ++index) {
      if (page_bytes[index] != '\n') {
        continue;
      }

      size_t line_end = index;
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }
      lines.push_back(parseLogfaultLine(page_bytes.substr(line_start, line_end - line_start),
                                        static_cast<uint32_t>(line_start)));
      line_start = index + 1U;
    }

    if (line_start < page_bytes.size()) {
      size_t line_end = page_bytes.size();
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }
      lines.push_back(parseLogfaultLine(page_bytes.substr(line_start, line_end - line_start),
                                        static_cast<uint32_t>(line_start)));
    }

    return lines;
  }
};

class NoneScanner final : public LogFormatScanner {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "None"; }

  [[nodiscard]] FastScanResult scanLineFast(std::string_view) const noexcept override {
    return {};
  }

  [[nodiscard]] std::vector<ParsedLineMetadata> buildLineIndex(
      std::string_view page_bytes) const override {
    std::vector<ParsedLineMetadata> lines;
    if (page_bytes.empty()) {
      return lines;
    }

    size_t line_start = 0;
    for (size_t index = 0; index < page_bytes.size(); ++index) {
      if (page_bytes[index] != '\n') {
        continue;
      }

      size_t line_end = index;
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }

      ParsedLineMetadata parsed;
      parsed.line_offset = static_cast<uint32_t>(line_start);
      parsed.line_length = static_cast<uint32_t>(line_end - line_start);
      parsed.message = ParsedSpan{0, parsed.line_length};
      lines.push_back(parsed);
      line_start = index + 1U;
    }

    if (line_start < page_bytes.size()) {
      size_t line_end = page_bytes.size();
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }

      ParsedLineMetadata parsed;
      parsed.line_offset = static_cast<uint32_t>(line_start);
      parsed.line_length = static_cast<uint32_t>(line_end - line_start);
      parsed.message = ParsedSpan{0, parsed.line_length};
      lines.push_back(parsed);
    }

    return lines;
  }
};

}  // namespace

std::unique_ptr<LogFormatScanner> createDefaultLogScanner() {
  return std::make_unique<NoneScanner>();
}

std::unique_ptr<LogFormatScanner> createLogScannerByName(std::string_view requested_name) {
  if (requested_name.empty() || requested_name == "Auto"sv || requested_name == "None"sv) {
    return std::make_unique<NoneScanner>();
  }

  if (requested_name == "Logfault"sv) {
    return std::make_unique<LogfaultScanner>();
  }

  return createDefaultLogScanner();
}

QStringList availableLogScannerNames() {
  return {QStringLiteral("Auto"), QStringLiteral("None"), QStringLiteral("Logfault")};
}

}  // namespace lgx
