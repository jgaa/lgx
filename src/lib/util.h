#pragma once

#include <string_view>

#include <QString>
#include <QStringList>
#include <QProcess>
#include <qcorotask.h>

namespace lgx {

[[nodiscard]] QString applicationVersion();
[[nodiscard]] std::string_view applicationVersionView() noexcept;
[[nodiscard]] QCoro::Task<QString> applicationVersionAsync();
[[nodiscard]] bool isRunningInFlatpak() noexcept;
[[nodiscard]] bool canSpawnHostCommands();
[[nodiscard]] QString findHostExecutable(const QString& program_name);
[[nodiscard]] bool isHostExecutableFile(const QString& path);
void configureHostProcess(QProcess& process, const QString& program, const QStringList& arguments = {});

}  // namespace lgx
