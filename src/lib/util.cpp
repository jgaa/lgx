#include "util.h"

#include <boost/config.hpp>
#include <boost/version.hpp>

namespace lgx {

namespace {

QString boostVersionString() {
  const auto major = BOOST_VERSION / 100000;
  const auto minor = BOOST_VERSION / 100 % 1000;
  const auto patch = BOOST_VERSION % 100;

  return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString compilerString() {
  return QString::fromLatin1(BOOST_COMPILER);
}

}  // namespace

AppInfo::AppInfo(QObject *parent)
    : QObject(parent) {}

QString AppInfo::description() const {
  return tr("lgx is a desktop log viewer.");
}

QString AppInfo::applicationVersion() const {
  return lgx::applicationVersion();
}

QString AppInfo::qtVersion() const {
  return QString::fromLatin1(qVersion());
}

QString AppInfo::boostVersion() const {
  return boostVersionString();
}

QString AppInfo::compiler() const {
  return compilerString();
}

QString AppInfo::buildDate() const {
  return QStringLiteral(__DATE__ " " __TIME__);
}

QString applicationVersion() {
  return QString::fromLatin1(LGX_VERSION);
}

std::string_view applicationVersionView() noexcept {
  return LGX_VERSION;
}

QCoro::Task<QString> applicationVersionAsync() {
  co_return applicationVersion();
}

}  // namespace lgx
