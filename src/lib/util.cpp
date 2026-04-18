#include "util.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace lgx {

namespace {

QString flatpakSpawnPath() {
  return QStandardPaths::findExecutable(QStringLiteral("flatpak-spawn"));
}

QString runCapture(const QString& program, const QStringList& arguments, int timeout_ms = 2000) {
  QProcess process;
  process.setProgram(program);
  process.setArguments(arguments);
  process.start();
  if (!process.waitForFinished(timeout_ms)) {
    process.kill();
    process.waitForFinished(200);
    return {};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }
  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

}  // namespace

QString applicationVersion() {
  return QString::fromLatin1(LGX_VERSION);
}

std::string_view applicationVersionView() noexcept {
  return LGX_VERSION;
}

QCoro::Task<QString> applicationVersionAsync() {
  co_return applicationVersion();
}

bool isRunningInFlatpak() noexcept {
  static const bool in_flatpak = qEnvironmentVariableIsSet("FLATPAK_ID")
      || QFileInfo::exists(QStringLiteral("/.flatpak-info"));
  return in_flatpak;
}

bool canSpawnHostCommands() {
  static const bool can_spawn = !flatpakSpawnPath().isEmpty();
  return can_spawn;
}

QString findHostExecutable(const QString& program_name) {
  if (!isRunningInFlatpak() || !canSpawnHostCommands()) {
    return QStandardPaths::findExecutable(program_name);
  }

  const QString script = QStringLiteral("command -v -- \"$1\"");
  return runCapture(flatpakSpawnPath(),
                    {QStringLiteral("--host"),
                     QStringLiteral("sh"),
                     QStringLiteral("-lc"),
                     script,
                     QStringLiteral("sh"),
                     program_name});
}

bool isHostExecutableFile(const QString& path) {
  if (path.trimmed().isEmpty()) {
    return false;
  }

  if (!isRunningInFlatpak() || !canSpawnHostCommands()) {
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isExecutable();
  }

  QProcess process;
  process.setProgram(flatpakSpawnPath());
  process.setArguments({QStringLiteral("--host"),
                        QStringLiteral("test"),
                        QStringLiteral("-x"),
                        path});
  process.start();
  if (!process.waitForFinished(2000)) {
    process.kill();
    process.waitForFinished(200);
    return false;
  }
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void configureHostProcess(QProcess& process, const QString& program, const QStringList& arguments) {
  if (!isRunningInFlatpak() || !canSpawnHostCommands()) {
    process.setProgram(program);
    process.setArguments(arguments);
    return;
  }

  QStringList spawned_arguments{QStringLiteral("--host"), program};
  spawned_arguments.append(arguments);
  process.setProgram(flatpakSpawnPath());
  process.setArguments(spawned_arguments);
}

}  // namespace lgx
