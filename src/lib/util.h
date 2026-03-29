#pragma once

#include <string_view>

#include <QObject>
#include <QString>
#include <qcorotask.h>

namespace lgx {

class AppInfo final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString description READ description CONSTANT)
  Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
  Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
  Q_PROPERTY(QString boostVersion READ boostVersion CONSTANT)
  Q_PROPERTY(QString compiler READ compiler CONSTANT)
  Q_PROPERTY(QString buildDate READ buildDate CONSTANT)

 public:
  explicit AppInfo(QObject *parent = nullptr);

  [[nodiscard]] QString description() const;
  [[nodiscard]] QString applicationVersion() const;
  [[nodiscard]] QString qtVersion() const;
  [[nodiscard]] QString boostVersion() const;
  [[nodiscard]] QString compiler() const;
  [[nodiscard]] QString buildDate() const;
};

[[nodiscard]] QString applicationVersion();
[[nodiscard]] std::string_view applicationVersionView() noexcept;
[[nodiscard]] QCoro::Task<QString> applicationVersionAsync();

}  // namespace lgx
