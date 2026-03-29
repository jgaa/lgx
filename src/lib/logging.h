#pragma once

#define LOGFAULT_USE_TID_AS_NAME 1

#include <iostream>
#include <memory>

#include <QString>
#include <QSettings>
#include <logfault/logfault.h>

namespace lgx {

class LoggingController final {
 public:
  static constexpr int kDisabledLevel = 0;
  static constexpr int kInfoLevel = 4;

  LoggingController() = default;

  void initialize() const;
  void ensureDefaults(QSettings &settings) const;
  [[nodiscard]] QString settingsFilePath() const;
};

}  // namespace lgx

#define LOG_ERROR LFLOG_ERROR
#define LOG_WARN LFLOG_WARN
#define LOG_NOTICE LFLOG_NOTICE
#define LOG_INFO LFLOG_INFO
#define LOG_DEBUG LFLOG_DEBUG
#define LOG_TRACE LFLOG_TRACE

#define LOG_ERROR_N LFLOG_ERROR_EX
#define LOG_WARN_N LFLOG_WARN_EX
#define LOG_NOTICE_N LFLOG_NOTICE_EX
#define LOG_INFO_N LFLOG_INFO_EX
#define LOG_DEBUG_N LFLOG_DEBUG_EX
#define LOG_TRACE_N LFLOG_TRACE_EX
