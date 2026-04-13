#include "AppInfo.h"

#include <boost/config.hpp>
#include <boost/version.hpp>

#include "util.h"

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

QString AppInfo::tagline() const {
  return tr("lgx is a high-performance log explorer for developers and DevOps, designed to handle large files with fast indexing, smart filtering, and clear visual insights.");
}

QString AppInfo::description() const
{
    return tr(R"(LGX is built for people who spend too much time reading logs (by someone who absolutely does).

It handles large log files with ease, providing fast indexing, powerful filtering, and instant navigation. Whether you're chasing down an error, tracking system behavior, or scanning for anomalies, LGX helps you get to the important parts quickly.

LGX isn’t limited to static files. It can stream and explore logs directly from live sources, including Docker containers, systemd journals, Android logcat, and arbitrary shell command pipelines. This makes it equally useful for local debugging, remote troubleshooting, and real-time monitoring.

With clear visual cues, support for structured logs, and a strong focus on performance, LGX turns logs from a wall of text into something you can actually work with.)");
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

}  // namespace lgx
