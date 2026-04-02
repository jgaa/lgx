#include "logging.h"

#include <QFileInfo>

namespace lgx {

void LoggingController::ensureDefaults(QSettings &settings) const {
  if (!settings.contains(QStringLiteral("logging/applevel"))) {
    settings.setValue(QStringLiteral("logging/applevel"), kInfoLevel);
  }

  if (!settings.contains(QStringLiteral("logging/level"))) {
    settings.setValue(QStringLiteral("logging/level"), kDisabledLevel);
  }

  if (!settings.contains(QStringLiteral("logging/path"))) {
    settings.setValue(QStringLiteral("logging/path"), QString{});
  }

  if (!settings.contains(QStringLiteral("logging/prune"))) {
    settings.setValue(QStringLiteral("logging/prune"), QStringLiteral("false"));
  }
}

void LoggingController::initialize() const {
  QSettings settings;
  ensureDefaults(settings);
  settings.sync();

#ifdef Q_OS_LINUX
  if (const auto level = settings.value(QStringLiteral("logging/applevel"), kInfoLevel).toInt();
      level > kDisabledLevel) {
    logfault::LogManager::Instance().AddHandler(
      std::make_unique<logfault::StreamHandler>(std::clog, static_cast<logfault::LogLevel>(level)));
  }
#endif

  if (const auto level = settings.value(QStringLiteral("logging/level"), kDisabledLevel).toInt();
      level > kDisabledLevel) {
    const auto path = settings.value(QStringLiteral("logging/path")).toString().trimmed();
    if (!path.isEmpty()) {
      const auto prune = settings.value(QStringLiteral("logging/prune")).toString() == QStringLiteral("true");
      logfault::LogManager::Instance().AddHandler(
        std::make_unique<logfault::StreamHandler>(path.toStdString(),
                                                  static_cast<logfault::LogLevel>(level),
                                                  prune));
    }
  }
}

QString LoggingController::settingsFilePath() const {
  return QFileInfo(QSettings{}.fileName()).absoluteFilePath();
}

}  // namespace lgx
