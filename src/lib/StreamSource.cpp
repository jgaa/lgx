#include "StreamSource.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QUrlQuery>

#include "UiSettings.h"
#include "logging.h"

namespace lgx {

namespace {

constexpr auto kPipeScheme = "pipe";
constexpr auto kDockerScheme = "docker";
constexpr auto kAdbScheme = "adb";
constexpr auto kSpoolFileName = "stream.log";
constexpr auto kAppCacheRootName = "lgx";
constexpr auto kSpoolDirName = "spool";

QString streamSpoolTemplate() {
  QString cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cache_root.isEmpty()) {
    cache_root = QDir::tempPath();
  }

  QDir cache_dir(cache_root);
  const QString spool_root = QStringLiteral("%1/%2")
                                 .arg(QLatin1StringView{kAppCacheRootName},
                                      QLatin1StringView{kSpoolDirName});
  if (!cache_dir.mkpath(spool_root)) {
    return {};
  }

  return cache_dir.filePath(spool_root + QStringLiteral("/stream-XXXXXX"));
}

bool queryFlagValue(const QUrlQuery& query, QStringView key, bool default_value) {
  const auto value = query.queryItemValue(key.toString());
  if (value.isEmpty()) {
    return default_value;
  }

  return value != QStringLiteral("0")
      && value.compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0;
}

class PipeStreamProvider final : public IStreamProvider {
 public:
  PipeStreamProvider(QString command, bool capture_stdout, bool capture_stderr)
      : command_(std::move(command)),
        capture_stdout_(capture_stdout),
        capture_stderr_(capture_stderr) {
    QObject::connect(&process_, &QProcess::readyReadStandardOutput, &process_, [this]() {
      if (capture_stdout_) {
        if (callbacks_.on_bytes) {
          callbacks_.on_bytes(process_.readAllStandardOutput());
        }
      } else {
        process_.readAllStandardOutput();
      }
    });
    QObject::connect(&process_, &QProcess::readyReadStandardError, &process_, [this]() {
      if (capture_stderr_) {
        if (callbacks_.on_bytes) {
          callbacks_.on_bytes(process_.readAllStandardError());
        }
      } else {
        process_.readAllStandardError();
      }
    });
    QObject::connect(&process_, &QProcess::errorOccurred, &process_,
                     [this](QProcess::ProcessError error) {
                       if ((error == QProcess::Crashed || error == QProcess::FailedToStart)
                           && callbacks_.on_error) {
                         callbacks_.on_error(process_.errorString());
                       }
                     });
    QObject::connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &process_,
                     [this](int, QProcess::ExitStatus) {
                       if (callbacks_.on_finished) {
                         callbacks_.on_finished();
                       }
                     });
  }

  void setCallbacks(Callbacks callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  void start() override {
    process_.setProgram(QStringLiteral("/bin/sh"));
    process_.setArguments({QStringLiteral("-lc"), command_});
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    process_.start();
  }

  void stop() override {
    if (process_.state() == QProcess::NotRunning) {
      return;
    }

    callbacks_ = {};
    QObject::disconnect(&process_, nullptr, &process_, nullptr);
    process_.blockSignals(true);
    process_.terminate();
    if (!process_.waitForFinished(500)) {
      process_.kill();
      process_.waitForFinished(500);
    }
  }

 private:
  QProcess process_;
  Callbacks callbacks_;
  QString command_;
  bool capture_stdout_{true};
  bool capture_stderr_{true};
};

class DockerStreamProvider final : public IStreamProvider {
 public:
  explicit DockerStreamProvider(QString container_id)
      : container_id_(std::move(container_id)) {
    QObject::connect(&process_, &QProcess::readyReadStandardOutput, &process_, [this]() {
      if (callbacks_.on_bytes) {
        callbacks_.on_bytes(process_.readAllStandardOutput());
      } else {
        process_.readAllStandardOutput();
      }
    });
    QObject::connect(&process_, &QProcess::readyReadStandardError, &process_, [this]() {
      if (callbacks_.on_bytes) {
        callbacks_.on_bytes(process_.readAllStandardError());
      } else {
        process_.readAllStandardError();
      }
    });
    QObject::connect(&process_, &QProcess::errorOccurred, &process_,
                     [this](QProcess::ProcessError error) {
                       if ((error == QProcess::Crashed || error == QProcess::FailedToStart)
                           && callbacks_.on_error) {
                         callbacks_.on_error(process_.errorString());
                       }
                     });
    QObject::connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &process_,
                     [this](int, QProcess::ExitStatus) {
                       if (callbacks_.on_finished) {
                         callbacks_.on_finished();
                       }
                     });
  }

  void setCallbacks(Callbacks callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  void start() override {
    process_.setProgram(QStringLiteral("docker"));
    process_.setArguments({QStringLiteral("logs"), QStringLiteral("-f"),
                           QStringLiteral("--timestamps"), container_id_});
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    process_.start();
  }

  void stop() override {
    if (process_.state() == QProcess::NotRunning) {
      return;
    }

    callbacks_ = {};
    QObject::disconnect(&process_, nullptr, &process_, nullptr);
    process_.blockSignals(true);
    process_.terminate();
    if (!process_.waitForFinished(500)) {
      process_.kill();
      process_.waitForFinished(500);
    }
  }

 private:
  QProcess process_;
  Callbacks callbacks_;
  QString container_id_;
};

class AdbLogcatProvider final : public IStreamProvider {
 public:
  explicit AdbLogcatProvider(QString serial)
      : serial_(std::move(serial)) {
    QObject::connect(&process_, &QProcess::readyReadStandardOutput, &process_, [this]() {
      if (callbacks_.on_bytes) {
        callbacks_.on_bytes(process_.readAllStandardOutput());
      } else {
        process_.readAllStandardOutput();
      }
    });
    QObject::connect(&process_, &QProcess::readyReadStandardError, &process_, [this]() {
      if (callbacks_.on_bytes) {
        callbacks_.on_bytes(process_.readAllStandardError());
      } else {
        process_.readAllStandardError();
      }
    });
    QObject::connect(&process_, &QProcess::errorOccurred, &process_,
                     [this](QProcess::ProcessError error) {
                       if ((error == QProcess::Crashed || error == QProcess::FailedToStart)
                           && callbacks_.on_error) {
                         callbacks_.on_error(process_.errorString());
                       }
                     });
    QObject::connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &process_,
                     [this](int, QProcess::ExitStatus) {
                       if (callbacks_.on_finished) {
                         callbacks_.on_finished();
                       }
                     });
  }

  void setCallbacks(Callbacks callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  void start() override {
    const auto adb_path = UiSettings::instance().adbExecutablePath().trimmed();
    if (adb_path.isEmpty()) {
      LOG_WARN << "ADB logcat provider cannot start because adb executable path is empty";
      if (callbacks_.on_error) {
        callbacks_.on_error(QObject::tr("ADB executable path is not configured."));
      }
      return;
    }

    process_.setProgram(adb_path);
    process_.setArguments({QStringLiteral("-s"), serial_, QStringLiteral("logcat"),
                           QStringLiteral("-v"), QStringLiteral("threadtime")});
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    LOG_INFO << "Starting adb logcat provider with program='"
             << adb_path.toStdString() << "' serial='" << serial_.toStdString() << "'";
    process_.start();
  }

  void stop() override {
    if (process_.state() == QProcess::NotRunning) {
      return;
    }

    callbacks_ = {};
    QObject::disconnect(&process_, nullptr, &process_, nullptr);
    process_.blockSignals(true);
    process_.terminate();
    if (!process_.waitForFinished(500)) {
      process_.kill();
      process_.waitForFinished(500);
    }
  }

 private:
  QProcess process_;
  Callbacks callbacks_;
  QString serial_;
};

}  // namespace

StreamSource::StreamSource() {
  flush_timer_.setSingleShot(true);
  flush_timer_.setInterval(40);
  QObject::connect(&flush_timer_, &QTimer::timeout, &flush_timer_, [this]() {
    flushPendingBytes();
  });
}

StreamSource::~StreamSource() {
  close();
}

bool StreamSource::isSupportedUrl(const QUrl& url) {
  const auto scheme = url.scheme();
  return scheme == QLatin1StringView{kPipeScheme}
      || scheme == QLatin1StringView{kDockerScheme}
      || scheme == QLatin1StringView{kAdbScheme};
}

QUrl StreamSource::makePipeUrl(const QString& command, bool include_stdout,
                               bool include_stderr) {
  QUrl url;
  url.setScheme(QLatin1StringView{kPipeScheme});
  url.setPath(QStringLiteral("/") + QUuid::createUuid().toString(QUuid::WithoutBraces));

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("cmd"), command);
  query.addQueryItem(QStringLiteral("stdout"), include_stdout ? QStringLiteral("1") : QStringLiteral("0"));
  query.addQueryItem(QStringLiteral("stderr"), include_stderr ? QStringLiteral("1") : QStringLiteral("0"));
  url.setQuery(query);
  return url;
}

std::optional<PipeStreamSpec> StreamSource::parsePipeSpec(const QUrl& url) {
  if (url.scheme() != QLatin1StringView{kPipeScheme}) {
    return std::nullopt;
  }

  const QUrlQuery query(url);
  PipeStreamSpec spec;
  spec.instance_id = url.path();
  if (spec.instance_id.startsWith(QLatin1Char('/'))) {
    spec.instance_id.remove(0, 1);
  }
  spec.command = query.queryItemValue(QStringLiteral("cmd"));
  spec.capture_stdout = queryFlagValue(query, QStringLiteral("stdout"), true);
  spec.capture_stderr = queryFlagValue(query, QStringLiteral("stderr"), true);
  if (spec.command.trimmed().isEmpty() || (!spec.capture_stdout && !spec.capture_stderr)) {
    return std::nullopt;
  }

  return spec;
}

QUrl StreamSource::makeDockerUrl(const QString& container_id, const QString& container_name) {
  QUrl url;
  url.setScheme(QLatin1StringView{kDockerScheme});
  url.setPath(QStringLiteral("/") + QUuid::createUuid().toString(QUuid::WithoutBraces));

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("container"), container_id.trimmed());
  if (!container_name.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("name"), container_name.trimmed());
  }
  url.setQuery(query);
  return url;
}

std::optional<DockerStreamSpec> StreamSource::parseDockerSpec(const QUrl& url) {
  if (url.scheme() != QLatin1StringView{kDockerScheme}) {
    return std::nullopt;
  }

  const QUrlQuery query(url);
  DockerStreamSpec spec;
  spec.instance_id = url.path();
  if (spec.instance_id.startsWith(QLatin1Char('/'))) {
    spec.instance_id.remove(0, 1);
  }
  spec.container_id = query.queryItemValue(QStringLiteral("container")).trimmed();
  spec.container_name = query.queryItemValue(QStringLiteral("name")).trimmed();
  if (spec.container_id.isEmpty()) {
    return std::nullopt;
  }

  return spec;
}

QUrl StreamSource::makeAdbLogcatUrl(const QString& serial, const QString& name) {
  QUrl url;
  url.setScheme(QLatin1StringView{kAdbScheme});
  url.setPath(QStringLiteral("/") + QUuid::createUuid().toString(QUuid::WithoutBraces));

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("serial"), serial.trimmed());
  if (!name.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("name"), name.trimmed());
  }
  url.setQuery(query);
  return url;
}

std::optional<AdbLogcatSpec> StreamSource::parseAdbLogcatSpec(const QUrl& url) {
  if (url.scheme() != QLatin1StringView{kAdbScheme}) {
    return std::nullopt;
  }

  const QUrlQuery query(url);
  AdbLogcatSpec spec;
  spec.instance_id = url.path();
  if (spec.instance_id.startsWith(QLatin1Char('/'))) {
    spec.instance_id.remove(0, 1);
  }
  spec.serial = query.queryItemValue(QStringLiteral("serial")).trimmed();
  spec.name = query.queryItemValue(QStringLiteral("name")).trimmed();
  if (spec.serial.isEmpty()) {
    return std::nullopt;
  }

  return spec;
}

void StreamSource::setCallbacks(SourceCallbacks callbacks) {
  LogSource::setCallbacks(std::move(callbacks));
  spool_source_.setCallbacks(SourceCallbacks{
      .on_state_changed =
          [this](SourceSnapshot snapshot) {
            snapshot.following = following_;
            emitStateChanged(snapshot);
          },
      .on_lines_appended =
          [this](uint64_t first_new_line, uint64_t count) {
            emitLinesAppended(first_new_line, count);
          },
      .on_reset =
          [this](SourceResetReason reason) {
            emitReset(reason);
          },
      .on_error =
          [this](std::string message) {
            fail(QString::fromStdString(message));
          },
  });
}

std::string StreamSource::path() const {
  return spool_path_.toStdString();
}

void StreamSource::open(const std::string& path) {
  close();

  const QUrl url(QString::fromStdString(path));
  LOG_INFO << "Opening stream source '" << url.toString().toStdString() << "'";
  if (url.scheme() == QLatin1StringView{kAdbScheme}
      && spool_source_.requestedScannerName() == "Auto") {
    spool_source_.setRequestedScannerName("Logcat");
  }
  provider_ = createProvider(url);
  if (!provider_) {
    LOG_WARN << "No stream provider matched URL '" << url.toString().toStdString() << "'";
    fail(QObject::tr("Unsupported stream source URL"));
    return;
  }
  if (!ensureSpoolFile()) {
    provider_.reset();
    return;
  }

  provider_->setCallbacks(IStreamProvider::Callbacks{
      .on_bytes =
          [this](QByteArray bytes) {
            enqueueProviderBytes(std::move(bytes));
          },
      .on_error =
          [this](QString message) {
            fail(std::move(message));
          },
      .on_finished =
          [this]() {
            flushPendingBytes();
            refresh();
          },
  });

  spool_source_.open(spool_path_.toStdString());
  LOG_INFO << "Stream spool file ready at '" << spool_path_.toStdString() << "'";
  provider_->start();
  open_ = true;
}

void StreamSource::close() {
  if (!open_ && !provider_ && spool_path_.isEmpty() && !spool_file_.isOpen()
      && pending_bytes_.isEmpty()) {
    return;
  }

  open_ = false;
  flush_timer_.stop();
  if (provider_) {
    provider_->setCallbacks({});
    provider_->stop();
  }

  pending_bytes_.clear();
  failed_ = false;
  provider_.reset();
  spool_source_.setCallbacks({});
  spool_source_.close();
  if (spool_file_.isOpen()) {
    spool_file_.close();
  }
  spool_path_.clear();
  clearSpoolDirectory();
}

void StreamSource::startIndexing() {
  if (!failed_) {
    spool_source_.startIndexing();
  }
}

void StreamSource::refresh() {
  flushPendingBytes();
  if (!failed_) {
    spool_source_.refresh();
  }
}

void StreamSource::setFollowing(bool enabled) {
  if (following_ == enabled) {
    return;
  }

  following_ = enabled;
  auto next_snapshot = snapshot();
  next_snapshot.following = following_;
  emitStateChanged(next_snapshot);
}

std::string StreamSource::scannerName() const {
  return spool_source_.scannerName();
}

std::string StreamSource::requestedScannerName() const {
  return spool_source_.requestedScannerName();
}

void StreamSource::setRequestedScannerName(std::string name) {
  spool_source_.setRequestedScannerName(std::move(name));
}

SourceSnapshot StreamSource::snapshot() const {
  auto source_snapshot = spool_source_.snapshot();
  source_snapshot.following = following_;
  if (failed_) {
    source_snapshot.state = SourceState::Failed;
  }
  return source_snapshot;
}

uint64_t StreamSource::fileSize() const {
  return spool_source_.fileSize();
}

double StreamSource::linesPerSecond() const {
  return spool_source_.linesPerSecond();
}

void StreamSource::fetchLines(uint64_t first_line, size_t count,
                              std::function<void(SourceLines)> on_ready) {
  spool_source_.fetchLines(first_line, count, std::move(on_ready));
}

std::optional<uint64_t> StreamSource::nextLineWithLevel(uint64_t after_line,
                                                        LogLevel level) const {
  return spool_source_.nextLineWithLevel(after_line, level);
}

std::optional<uint64_t> StreamSource::previousLineWithLevel(uint64_t before_line,
                                                            LogLevel level) const {
  return spool_source_.previousLineWithLevel(before_line, level);
}

std::unique_ptr<IStreamProvider> StreamSource::createProvider(const QUrl& url) {
  if (const auto pipe_spec = parsePipeSpec(url); pipe_spec.has_value()) {
    LOG_INFO << "Using pipe stream provider";
    return std::make_unique<PipeStreamProvider>(
        pipe_spec->command, pipe_spec->capture_stdout, pipe_spec->capture_stderr);
  }
  if (const auto docker_spec = parseDockerSpec(url); docker_spec.has_value()) {
    LOG_INFO << "Using docker stream provider for container='"
             << docker_spec->container_id.toStdString() << "'";
    return std::make_unique<DockerStreamProvider>(docker_spec->container_id);
  }
  if (const auto adb_spec = parseAdbLogcatSpec(url); adb_spec.has_value()) {
    LOG_INFO << "Using adb logcat provider for serial='" << adb_spec->serial.toStdString() << "'";
    return std::make_unique<AdbLogcatProvider>(adb_spec->serial);
  }

  return {};
}

bool StreamSource::ensureSpoolFile() {
  if (!spool_dir_) {
    const auto spool_template = streamSpoolTemplate();
    if (spool_template.isEmpty()) {
      fail(QObject::tr("Failed to prepare cache directory for stream spool"));
      return false;
    }

    auto spool_dir = std::make_unique<QTemporaryDir>(spool_template);
    spool_dir->setAutoRemove(true);
    if (!spool_dir->isValid()) {
      fail(QObject::tr("Failed to create temporary directory for stream spool"));
      return false;
    }

    spool_dir_ = std::move(spool_dir);
  }

  spool_path_ = QDir(spool_dir_->path()).filePath(QLatin1StringView{kSpoolFileName});
  spool_file_.setFileName(spool_path_);
  if (!spool_file_.open(QIODevice::WriteOnly | QIODevice::Append)) {
    fail(QObject::tr("Failed to open stream spool file: %1").arg(spool_path_));
    return false;
  }

  return true;
}

void StreamSource::enqueueProviderBytes(QByteArray bytes) {
  if (bytes.isEmpty() || failed_) {
    return;
  }

  pending_bytes_.append(std::move(bytes));
  if (pending_bytes_.size() >= 64 * 1024) {
    flushPendingBytes();
    return;
  }

  if (!flush_timer_.isActive()) {
    flush_timer_.start();
  }
}

void StreamSource::flushPendingBytes() {
  flush_timer_.stop();
  if (pending_bytes_.isEmpty() || failed_) {
    return;
  }

  if (!spool_file_.isOpen() && !spool_file_.open(QIODevice::WriteOnly | QIODevice::Append)) {
    fail(QObject::tr("Failed to reopen stream spool file: %1").arg(spool_path_));
    return;
  }

  qint64 written_total = 0;
  while (written_total < pending_bytes_.size()) {
    const auto written = spool_file_.write(pending_bytes_.constData() + written_total,
                                           pending_bytes_.size() - written_total);
    if (written <= 0) {
      fail(QObject::tr("Failed to write to stream spool file: %1").arg(spool_path_));
      return;
    }
    written_total += written;
  }

  spool_file_.flush();
  pending_bytes_.clear();
  spool_source_.refresh();
}

void StreamSource::clearSpoolDirectory() {
  if (!spool_dir_) {
    return;
  }

  spool_dir_->remove();
  spool_dir_.reset();
}

void StreamSource::fail(QString message) {
  failed_ = true;
  flush_timer_.stop();
  pending_bytes_.clear();
  LOG_ERROR << "Stream source failed: " << message.toStdString();
  emitError(message.toStdString());
  emitStateChanged(snapshot());
}

}  // namespace lgx
