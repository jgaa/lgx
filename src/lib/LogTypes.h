#pragma once

#include <QString>
#include <QStringList>
#include <QDateTime>

namespace lgx {

/**
 * @brief Parsed log level for a log line.
 */
enum LogLevel {
  LogLevel_Error = 0,
  LogLevel_Warn = 1,
  LogLevel_Notice = 2,
  LogLevel_Info = 3,
  LogLevel_Debug = 4,
  LogLevel_Trace = 5
};

inline constexpr size_t number_of_log_levels = 6;

/**
 * @brief Mark slot for a log line.
 *
 * The enum identifies logical mark slots, not hard-coded final colors. UI
 * settings currently map these slots to green/red/orange/yellow/blue/cyan/black
 * defaults and can later remap them to user-defined colors.
 */
enum LineMarkColor {
  LineMark_None = 0,
  LineMark_Default = 1,
  LineMark_Accent1 = 2,
  LineMark_Accent2 = 3,
  LineMark_Accent3 = 4,
  LineMark_Accent4 = 5,
  LineMark_Accent5 = 6,
  LineMark_Accent6 = 7
};

inline constexpr size_t number_of_line_mark_slots = 7;

/**
 * @brief One logical row exposed by LogModel.
 */
struct LogRow {
  qsizetype line_no{};
  qint64 pid{};
  qint64 tid{};
  QString function_name;
  LogLevel log_level{LogLevel_Info};
  LineMarkColor mark_color{LineMark_None};
  QString raw_message;
  QString message;
  QDateTime date;
  QStringList tags;
  QString thread_id;
};

struct TextSpan {
  int32_t offset{-1};
  uint32_t length{};

  [[nodiscard]] bool isSet() const noexcept {
    return offset >= 0;
  }
};

}  // namespace lgx
