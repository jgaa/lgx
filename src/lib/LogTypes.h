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
 * @brief One logical row exposed by LogModel.
 */
struct LogRow {
  qsizetype line_no{};
  QString function_name;
  LogLevel log_level{LogLevel_Info};
  QString raw_message;
  QString message;
  QDateTime date;
  QStringList tags;
  QString thread_id;
};

}  // namespace lgx
