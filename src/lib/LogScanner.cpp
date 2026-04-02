#include "LogScanner.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <string_view>

namespace lgx {
namespace {

using namespace std::literals;

[[nodiscard]] bool isDigit(char ch) noexcept {
  return ch >= '0' && ch <= '9';
}

[[nodiscard]] bool isAlpha(char ch) noexcept {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
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

struct TimestampPrefix {
  int64_t msecs_since_epoch{};
  size_t parsed_length{};
};

[[nodiscard]] std::optional<TimestampPrefix> parseGenericTimestampPrefix(
    std::string_view line) noexcept {
  if (line.size() < 10) {
    return std::nullopt;
  }

  const auto first_separator = line[4];
  if ((first_separator != '-' && first_separator != '/') || line[7] != first_separator) {
    return std::nullopt;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  if (!parseFixedDigits(line, 0, 4, year) || !parseFixedDigits(line, 5, 2, month) ||
      !parseFixedDigits(line, 8, 2, day)) {
    return std::nullopt;
  }

  const QDate date(year, month, day);
  if (!date.isValid()) {
    return std::nullopt;
  }

  size_t cursor = 10;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millis = 0;
  bool has_time = false;

  if (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == 'T')) {
    const auto time_start = cursor + 1U;
    size_t hour_digits = 0;
    while (time_start + hour_digits < line.size() && hour_digits < 2
           && isDigit(line[time_start + hour_digits])) {
      ++hour_digits;
    }

    if ((hour_digits == 1 || hour_digits == 2) && time_start + hour_digits + 6U <= line.size()
        && line[time_start + hour_digits] == ':'
        && line[time_start + hour_digits + 3U] == ':'
        && parseFixedDigits(line, time_start, hour_digits, hour)
        && parseFixedDigits(line, time_start + hour_digits + 1U, 2, minute)
        && parseFixedDigits(line, time_start + hour_digits + 4U, 2, second)) {
      has_time = true;
      cursor = time_start + hour_digits + 6U;

      if (cursor < line.size() && line[cursor] == '.') {
        size_t digit_count = 0;
        int fractional = 0;
        size_t fractional_cursor = cursor + 1U;
        while (fractional_cursor < line.size() && isDigit(line[fractional_cursor])) {
          if (digit_count < 3) {
            fractional = (fractional * 10) + (line[fractional_cursor] - '0');
          }
          ++fractional_cursor;
          ++digit_count;
        }
        if (digit_count > 0) {
          while (digit_count < 3) {
            fractional *= 10;
            ++digit_count;
          }
          millis = fractional;
          cursor = fractional_cursor;
        }
      }

      if (cursor < line.size() && line[cursor] == 'Z') {
        ++cursor;
      } else if (cursor < line.size() && (line[cursor] == '+' || line[cursor] == '-')) {
        const auto timezone_start = cursor + 1U;
        if (timezone_start + 2U <= line.size()) {
          int timezone_hour = 0;
          if (parseFixedDigits(line, timezone_start, 2, timezone_hour)) {
            cursor = timezone_start + 2U;
            if (cursor < line.size() && line[cursor] == ':') {
              if (cursor + 3U <= line.size()) {
                int timezone_minute = 0;
                if (parseFixedDigits(line, cursor + 1U, 2, timezone_minute)) {
                  cursor += 3U;
                }
              }
            } else if (cursor + 2U <= line.size()) {
              int timezone_minute = 0;
              if (parseFixedDigits(line, cursor, 2, timezone_minute)) {
                cursor += 2U;
              }
            }
          }
        }
      }
    }
  }

  const QTime time = has_time ? QTime(hour, minute, second, millis) : QTime(0, 0);
  if (!time.isValid()) {
    return std::nullopt;
  }

  const QDateTime date_time(date, time, QTimeZone::systemTimeZone());
  if (!date_time.isValid()) {
    return std::nullopt;
  }

  while (cursor < line.size() && line[cursor] == ' ') {
    ++cursor;
  }

  return TimestampPrefix{.msecs_since_epoch = date_time.toMSecsSinceEpoch(), .parsed_length = cursor};
}

[[nodiscard]] bool startsLogfaultRecord(std::string_view line) noexcept {
  return parseTimestampMillis(line).has_value();
}

[[nodiscard]] std::optional<int64_t> parseLogcatTimestampMillis(
    std::string_view line) noexcept {
  if (line.size() < 18 || line[2] != '-' || line[5] != ' ' || line[8] != ':'
      || line[11] != ':' || line[14] != '.') {
    return std::nullopt;
  }

  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millis = 0;
  if (!parseFixedDigits(line, 0, 2, month) || !parseFixedDigits(line, 3, 2, day)
      || !parseFixedDigits(line, 6, 2, hour) || !parseFixedDigits(line, 9, 2, minute)
      || !parseFixedDigits(line, 12, 2, second) || !parseFixedDigits(line, 15, 3, millis)) {
    return std::nullopt;
  }

  const auto now = QDate::currentDate();
  const QDate date(now.year(), month, day);
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

[[nodiscard]] std::optional<uint32_t> parseUnsignedToken(std::string_view token) noexcept {
  if (token.empty()) {
    return std::nullopt;
  }

  uint32_t value = 0;
  const auto* begin = token.data();
  const auto* end = token.data() + token.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }

  return value;
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

[[nodiscard]] LogLevel parseLogcatLevelToken(std::string_view token) noexcept {
  while (!token.empty() && token.front() == ' ') {
    token.remove_prefix(1);
  }
  while (!token.empty() && token.back() == ' ') {
    token.remove_suffix(1);
  }
  if (token.empty()) {
    return LogLevel_Info;
  }

  switch (token.front()) {
    case 'E':
    case 'F':
    case 'A':
    return LogLevel_Error;
    case 'W':
      return LogLevel_Warn;
    case 'I':
      return LogLevel_Info;
    case 'D':
      return LogLevel_Debug;
    case 'V':
    case 'T':
      return LogLevel_Trace;
    default:
      return LogLevel_Info;
  }
}

[[nodiscard]] bool equalsIgnoreCase(std::string_view token, std::string_view needle) noexcept {
  if (token.size() != needle.size()) {
    return false;
  }

  for (size_t index = 0; index < token.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(token[index]))
        != std::tolower(static_cast<unsigned char>(needle[index]))) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] std::optional<LogLevel> parseGenericLevelWord(std::string_view token) noexcept {
  if (token.empty()) {
    return std::nullopt;
  }

  if (equalsIgnoreCase(token, "error") || equalsIgnoreCase(token, "err")
      || equalsIgnoreCase(token, "fatal") || equalsIgnoreCase(token, "panic")
      || equalsIgnoreCase(token, "critical") || equalsIgnoreCase(token, "crit")
      || equalsIgnoreCase(token, "alert") || equalsIgnoreCase(token, "emerg")) {
    return LogLevel_Error;
  }

  if (equalsIgnoreCase(token, "warning") || equalsIgnoreCase(token, "warn")
      || equalsIgnoreCase(token, "wrn") || equalsIgnoreCase(token, "w")) {
    return LogLevel_Warn;
  }

  if (equalsIgnoreCase(token, "notice") || equalsIgnoreCase(token, "note")
      || equalsIgnoreCase(token, "n")) {
    return LogLevel_Notice;
  }

  if (equalsIgnoreCase(token, "info") || equalsIgnoreCase(token, "information")
      || equalsIgnoreCase(token, "inf") || equalsIgnoreCase(token, "i")) {
    return LogLevel_Info;
  }

  if (equalsIgnoreCase(token, "debug") || equalsIgnoreCase(token, "dbg")
      || equalsIgnoreCase(token, "d")) {
    return LogLevel_Debug;
  }

  if (equalsIgnoreCase(token, "trace") || equalsIgnoreCase(token, "trc")
      || equalsIgnoreCase(token, "verbose") || equalsIgnoreCase(token, "v")
      || equalsIgnoreCase(token, "t")) {
    return LogLevel_Trace;
  }

  return std::nullopt;
}

struct GenericLevelMatch {
  LogLevel level{LogLevel_Info};
  size_t start{};
  size_t end{};
};

[[nodiscard]] std::optional<GenericLevelMatch> findGenericLevelNearStart(
    std::string_view line, size_t start_offset) noexcept {
  if (start_offset >= line.size()) {
    return std::nullopt;
  }

  constexpr size_t kMaxProbeChars = 48;
  constexpr size_t kMaxProbeTokens = 4;
  const auto probe_end = std::min(line.size(), start_offset + kMaxProbeChars);

  size_t token_count = 0;
  size_t token_start = start_offset;
  while (token_start < probe_end && token_count < kMaxProbeTokens) {
    while (token_start < probe_end && line[token_start] == ' ') {
      ++token_start;
    }
    if (token_start >= probe_end) {
      break;
    }

    size_t token_end = token_start;
    while (token_end < probe_end && line[token_end] != ' ') {
      ++token_end;
    }

    size_t word_start = token_start;
    while (word_start < token_end) {
      while (word_start < token_end && !isAlpha(line[word_start])) {
        ++word_start;
      }
      if (word_start >= token_end) {
        break;
      }

      size_t word_end = word_start;
      while (word_end < token_end && isAlpha(line[word_end])) {
        ++word_end;
      }

      if (const auto level = parseGenericLevelWord(line.substr(word_start, word_end - word_start))) {
        return GenericLevelMatch{.level = *level, .start = word_start, .end = token_end};
      }

      word_start = word_end;
    }

    ++token_count;
    token_start = token_end;
  }

  return std::nullopt;
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

[[nodiscard]] ParsedLineMetadata parseLogcatLine(std::string_view line,
                                                 uint32_t line_offset) noexcept {
  ParsedLineMetadata parsed;
  parsed.line_offset = line_offset;
  parsed.line_length = static_cast<uint32_t>(line.size());
  parsed.log_level = LogLevel_Info;
  parsed.message = ParsedSpan{0, static_cast<uint32_t>(line.size())};

  const auto timestamp_msecs = parseLogcatTimestampMillis(line);
  if (!timestamp_msecs) {
    return parsed;
  }

  parsed.has_timestamp = true;
  parsed.timestamp_msecs_since_epoch = *timestamp_msecs;

  auto token = nextToken(line, 18);
  if (!token) {
    return parsed;
  }

  const auto pid_token = *token;
  if (const auto pid = parseUnsignedToken(line.substr(pid_token.first, pid_token.second - pid_token.first))) {
    parsed.pid = *pid;
  }

  token = nextToken(line, pid_token.second);
  if (!token) {
    return parsed;
  }
  const auto tid_token = *token;
  if (const auto tid = parseUnsignedToken(line.substr(tid_token.first, tid_token.second - tid_token.first))) {
    parsed.tid = *tid;
  } else {
    parsed.thread_id = ParsedSpan{static_cast<uint32_t>(token->first),
                                  static_cast<uint32_t>(token->second - token->first)};
  }

  token = nextToken(line, tid_token.second);
  if (!token) {
    return parsed;
  }
  parsed.log_level = parseLogcatLevelToken(
      line.substr(token->first, token->second - token->first));

  size_t rest_start = token->second;
  while (rest_start < line.size() && line[rest_start] == ' ') {
    ++rest_start;
  }
  if (rest_start >= line.size()) {
    return parsed;
  }

  parsed.message = ParsedSpan{static_cast<uint32_t>(rest_start),
                              static_cast<uint32_t>(line.size() - rest_start)};

  if (const auto colon = line.find(':', rest_start); colon != std::string_view::npos) {
    parsed.function_name =
        ParsedSpan{static_cast<uint32_t>(rest_start),
                   static_cast<uint32_t>(colon - rest_start)};

    size_t message_start = colon + 1U;
    while (message_start < line.size() && line[message_start] == ' ') {
      ++message_start;
    }

    parsed.message = ParsedSpan{static_cast<uint32_t>(message_start),
                                static_cast<uint32_t>(line.size() - message_start)};
  }

  return parsed;
}

[[nodiscard]] ParsedLineMetadata parseGenericLine(std::string_view line,
                                                  uint32_t line_offset) noexcept {
  ParsedLineMetadata parsed;
  parsed.line_offset = line_offset;
  parsed.line_length = static_cast<uint32_t>(line.size());
  parsed.log_level = LogLevel_Info;
  parsed.message = ParsedSpan{0, static_cast<uint32_t>(line.size())};

  const auto timestamp = parseGenericTimestampPrefix(line);
  if (!timestamp) {
    return parsed;
  }

  parsed.has_timestamp = true;
  parsed.timestamp_msecs_since_epoch = timestamp->msecs_since_epoch;

  if (const auto level = findGenericLevelNearStart(line, timestamp->parsed_length)) {
    parsed.log_level = level->level;
    size_t message_start = level->end;
    while (message_start < line.size()
           && (line[message_start] == ' ' || line[message_start] == ':' || line[message_start] == '-')) {
      ++message_start;
    }
    parsed.message = ParsedSpan{static_cast<uint32_t>(message_start),
                                static_cast<uint32_t>(line.size() - message_start)};
  } else {
    parsed.message = ParsedSpan{static_cast<uint32_t>(timestamp->parsed_length),
                                static_cast<uint32_t>(line.size() - timestamp->parsed_length)};
  }

  return parsed;
}

class LogfaultScanner final : public LogFormatScanner {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "Logfault"; }
  [[nodiscard]] bool startsLogicalLine(std::string_view physical_line) const noexcept override {
    return startsLogfaultRecord(physical_line);
  }

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

    std::optional<ParsedLineMetadata> current;
    size_t line_start = 0;
    for (size_t index = 0; index < page_bytes.size(); ++index) {
      if (page_bytes[index] != '\n') {
        continue;
      }

      size_t line_end = index;
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }
      const auto line = page_bytes.substr(line_start, line_end - line_start);
      if (!current || startsLogfaultRecord(line)) {
        if (current) {
          lines.push_back(*current);
        }
        current = parseLogfaultLine(line, static_cast<uint32_t>(line_start));
      } else {
        current->line_length = static_cast<uint32_t>(line_end - current->line_offset);
      }
      line_start = index + 1U;
    }

    if (line_start < page_bytes.size()) {
      size_t line_end = page_bytes.size();
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }
      const auto line = page_bytes.substr(line_start, line_end - line_start);
      if (!current || startsLogfaultRecord(line)) {
        if (current) {
          lines.push_back(*current);
        }
        current = parseLogfaultLine(line, static_cast<uint32_t>(line_start));
      } else {
        current->line_length = static_cast<uint32_t>(line_end - current->line_offset);
      }
    }

    if (current) {
      lines.push_back(*current);
    }

    return lines;
  }
};

class LogcatScanner final : public LogFormatScanner {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "Logcat"; }
  [[nodiscard]] bool startsLogicalLine(std::string_view) const noexcept override { return true; }

  [[nodiscard]] FastScanResult scanLineFast(std::string_view line) const noexcept override {
    const auto parsed = parseLogcatLine(line, 0);
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
      lines.push_back(parseLogcatLine(page_bytes.substr(line_start, line_end - line_start),
                                      static_cast<uint32_t>(line_start)));
      line_start = index + 1U;
    }

    if (line_start < page_bytes.size()) {
      size_t line_end = page_bytes.size();
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }
      lines.push_back(parseLogcatLine(page_bytes.substr(line_start, line_end - line_start),
                                      static_cast<uint32_t>(line_start)));
    }

    return lines;
  }
};

class NoneScanner final : public LogFormatScanner {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "None"; }
  [[nodiscard]] bool startsLogicalLine(std::string_view) const noexcept override { return true; }

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

class GenericScanner final : public LogFormatScanner {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "Generic"; }

  [[nodiscard]] bool startsLogicalLine(std::string_view physical_line) const noexcept override {
    if (physical_line.empty()) {
      return true;
    }
    if (physical_line.front() == ' ' || physical_line.front() == '\t') {
      return false;
    }
    return parseGenericTimestampPrefix(physical_line).has_value();
  }

  [[nodiscard]] FastScanResult scanLineFast(std::string_view line) const noexcept override {
    const auto parsed = parseGenericLine(line, 0);
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

    std::optional<ParsedLineMetadata> current;
    size_t line_start = 0;
    for (size_t index = 0; index < page_bytes.size(); ++index) {
      if (page_bytes[index] != '\n') {
        continue;
      }

      size_t line_end = index;
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }
      const auto line = page_bytes.substr(line_start, line_end - line_start);
      if (!current || startsLogicalLine(line)) {
        if (current) {
          lines.push_back(*current);
        }
        current = parseGenericLine(line, static_cast<uint32_t>(line_start));
      } else {
        current->line_length = static_cast<uint32_t>(line_end - current->line_offset);
      }
      line_start = index + 1U;
    }

    if (line_start < page_bytes.size()) {
      size_t line_end = page_bytes.size();
      if (line_end > line_start && page_bytes[line_end - 1U] == '\r') {
        --line_end;
      }
      const auto line = page_bytes.substr(line_start, line_end - line_start);
      if (!current || startsLogicalLine(line)) {
        if (current) {
          lines.push_back(*current);
        }
        current = parseGenericLine(line, static_cast<uint32_t>(line_start));
      } else {
        current->line_length = static_cast<uint32_t>(line_end - current->line_offset);
      }
    }

    if (current) {
      lines.push_back(*current);
    }

    return lines;
  }
};

}  // namespace

std::unique_ptr<LogFormatScanner> createDefaultLogScanner() {
  return std::make_unique<GenericScanner>();
}

std::unique_ptr<LogFormatScanner> createLogScannerByName(std::string_view requested_name) {
  if (requested_name.empty() || requested_name == "Auto"sv || requested_name == "Generic"sv) {
    return std::make_unique<GenericScanner>();
  }

  if (requested_name == "None"sv) {
    return std::make_unique<NoneScanner>();
  }

  if (requested_name == "Logfault"sv) {
    return std::make_unique<LogfaultScanner>();
  }

  if (requested_name == "Logcat"sv) {
    return std::make_unique<LogcatScanner>();
  }

  return createDefaultLogScanner();
}

QStringList availableLogScannerNames() {
  return {QStringLiteral("Auto"), QStringLiteral("Generic"), QStringLiteral("None"),
          QStringLiteral("Logfault"), QStringLiteral("Logcat")};
}

}  // namespace lgx
