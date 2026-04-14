#include "StreamSource.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QFile>
#include <QProcess>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QUrlQuery>

#ifdef LGX_ENABLE_SYSTEMD_SOURCE
#include <systemd/sd-journal.h>
#ifdef LOG_NOTICE
#undef LOG_NOTICE
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#endif

#include "UiSettings.h"
#include "logging.h"

namespace lgx {

namespace {

constexpr auto kPipeScheme = "pipe";
constexpr auto kDockerScheme = "docker";
constexpr auto kAdbScheme = "adb";
constexpr auto kSystemdScheme = "systemd";
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
  DockerStreamProvider(QString container_id, bool no_history)
      : container_id_(std::move(container_id)),
        no_history_(no_history) {
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
    QStringList arguments{
        QStringLiteral("logs"),
        QStringLiteral("-f"),
        QStringLiteral("--timestamps"),
    };
    if (no_history_) {
      arguments.push_back(QStringLiteral("--since"));
      arguments.push_back(QStringLiteral("0s"));
    }
    arguments.push_back(container_id_);
    process_.setArguments(arguments);
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
  bool no_history_{false};
};

class AdbLogcatProvider final : public IStreamProvider {
 public:
  AdbLogcatProvider(QString serial, bool no_history)
      : serial_(std::move(serial)),
        no_history_(no_history) {
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
    QStringList arguments{
        QStringLiteral("-s"),
        serial_,
        QStringLiteral("logcat"),
    };
    if (no_history_) {
      arguments.push_back(QStringLiteral("-T"));
      arguments.push_back(QDateTime::currentDateTime().toString(QStringLiteral("MM-dd hh:mm:ss.zzz")));
    }
    arguments.push_back(QStringLiteral("-v"));
    arguments.push_back(QStringLiteral("threadtime"));
    process_.setArguments(arguments);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    LOG_INFO << "Starting adb logcat provider with program='"
             << adb_path.toStdString() << "' serial='" << serial_.toStdString()
             << "' noHistory=" << (no_history_ ? "true" : "false");
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
  bool no_history_{false};
};

#ifdef LGX_ENABLE_SYSTEMD_SOURCE
class SystemdJournalProvider final : public IStreamProvider {
 public:
  explicit SystemdJournalProvider(QString process_name, QString start_mode)
      : process_name_(std::move(process_name)),
        start_mode_(std::move(start_mode)) {
    drain_timer_.setSingleShot(true);
    QObject::connect(&drain_timer_, &QTimer::timeout, &drain_timer_, [this]() {
      drainAvailableEntries();
    });
    journal_poll_timer_.setInterval(250);
    QObject::connect(&journal_poll_timer_, &QTimer::timeout, &journal_poll_timer_, [this]() {
      processJournalChanges();
    });
  }

  ~SystemdJournalProvider() override {
    stop();
  }

  void setCallbacks(Callbacks callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  void start() override {
    if (journal_) {
      return;
    }

    sd_journal* journal = nullptr;
    const int open_result = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
    if (open_result < 0 || !journal) {
      if (callbacks_.on_error) {
        callbacks_.on_error(QObject::tr("Failed to open the systemd journal."));
      }
      return;
    }
    journal_ = journal;

    if (!process_name_.trimmed().isEmpty()) {
      const QByteArray comm_match = QStringLiteral("_COMM=%1").arg(process_name_.trimmed()).toUtf8();
      const QByteArray ident_match =
          QStringLiteral("SYSLOG_IDENTIFIER=%1").arg(process_name_.trimmed()).toUtf8();
      sd_journal_add_match(journal_, comm_match.constData(), 0);
      sd_journal_add_disjunction(journal_);
      sd_journal_add_match(journal_, ident_match.constData(), 0);
    }

    int seek_result = sd_journal_seek_head(journal_);
    if (start_mode_ == QStringLiteral("now")) {
      seek_result =
          sd_journal_seek_realtime_usec(journal_,
                                        static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch())
                                            * 1000U);
    } else if (start_mode_ == QStringLiteral("today")) {
      const auto now = QDateTime::currentDateTime();
      const auto midnight = QDateTime(now.date(), QTime(0, 0), now.timeZone());
      seek_result = sd_journal_seek_realtime_usec(
          journal_, static_cast<uint64_t>(midnight.toMSecsSinceEpoch()) * 1000U);
    } else if (start_mode_ == QStringLiteral("7d")) {
      seek_result =
          sd_journal_seek_realtime_usec(journal_,
                                        static_cast<uint64_t>(
                                            QDateTime::currentDateTime().addDays(-7).toMSecsSinceEpoch())
                                            * 1000U);
    }
    if (seek_result < 0) {
      if (callbacks_.on_error) {
        callbacks_.on_error(QObject::tr("Failed to seek the systemd journal."));
      }
      stop();
      return;
    }
    const int fd = sd_journal_get_fd(journal_);
    if (fd < 0) {
      if (callbacks_.on_error) {
        callbacks_.on_error(QObject::tr("Failed to watch the systemd journal."));
      }
      stop();
      return;
    }

    notifier_.reset(new QSocketNotifier(fd, QSocketNotifier::Read));
    QObject::connect(notifier_.get(), &QSocketNotifier::activated, notifier_.get(), [this]() {
      processJournalChanges();
    });
    journal_poll_timer_.start();
    drain_timer_.start(0);
  }

  void stop() override {
    callbacks_ = {};
    drain_timer_.stop();
    journal_poll_timer_.stop();
    if (notifier_) {
      QObject::disconnect(notifier_.get(), nullptr, notifier_.get(), nullptr);
      notifier_.reset();
    }
    if (journal_) {
      sd_journal_close(journal_);
      journal_ = nullptr;
    }
  }

 private:
  static QByteArray journalField(sd_journal* journal, const char* key) {
    const void* data = nullptr;
    size_t size = 0;
    if (sd_journal_get_data(journal, key, &data, &size) < 0 || !data || size == 0) {
      return {};
    }

    QByteArray field(static_cast<const char*>(data), static_cast<qsizetype>(size));
    const auto equals = field.indexOf('=');
    if (equals < 0) {
      return {};
    }
    field.remove(0, equals + 1);
    return field;
  }

  static QByteArray displayField(QByteArray value) {
    value.replace('\t', ' ');
    value.replace('\r', "\\r");
    value.replace('\n', "\\n");
    return value;
  }

  static QByteArray priorityName(const QByteArray& priority) {
    bool ok = false;
    const auto value = priority.toUInt(&ok);
    if (!ok) {
      return "INFO";
    }
    if (value <= 3U) {
      return "ERROR";
    }
    if (value == 4U) {
      return "WARN";
    }
    if (value == 5U) {
      return "NOTICE";
    }
    if (value == 7U) {
      return "DEBUG";
    }
    return "INFO";
  }

  QByteArray processNameForPid(uint32_t pid) const {
    if (pid == 0U) {
      return {};
    }
    if (const auto cached = process_name_cache_.constFind(pid); cached != process_name_cache_.constEnd()) {
      return cached.value();
    }

    QFile comm_file(QStringLiteral("/proc/%1/comm").arg(pid));
    QByteArray name;
    if (comm_file.open(QIODevice::ReadOnly)) {
      name = comm_file.readLine().trimmed();
    }
    process_name_cache_.insert(pid, name);
    return name;
  }

  QByteArray currentEntryLine() const {
    uint64_t realtime_usecs = 0;
    sd_journal_get_realtime_usec(journal_, &realtime_usecs);

    const auto priority = journalField(journal_, "PRIORITY");
    const auto message = journalField(journal_, "MESSAGE");
    const auto pid_field = journalField(journal_, "_PID");
    bool pid_ok = false;
    const auto pid = pid_field.toUInt(&pid_ok);
    QByteArray process = journalField(journal_, "_COMM");
    if (process.isEmpty()) {
      process = journalField(journal_, "SYSLOG_IDENTIFIER");
    }
    if (process.isEmpty() && pid_ok) {
      process = processNameForPid(pid);
    }

    QByteArray process_label = displayField(process);
    if (pid_ok && pid > 0U) {
      process_label += '[';
      process_label += QByteArray::number(pid);
      process_label += ']';
    } else if (process_label.isEmpty()) {
      process_label = "unknown";
    }

    const auto timestamp =
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(realtime_usecs / 1000U))
            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            .toUtf8();

    QByteArray line;
    line.reserve(128 + message.size());
    line += timestamp;
    line += ' ';
    line += priorityName(priority);
    line += ' ';
    line += process_label;
    line += ": ";
    line += displayField(message);
    line += '\n';
    return line;
  }

  void drainAvailableEntries() {
    if (!journal_) {
      return;
    }

    QByteArray batch;
    constexpr int kMaxEntriesPerDrain = 1000;
    int drained_count = 0;
    for (; drained_count < kMaxEntriesPerDrain; ++drained_count) {
      const int next = sd_journal_next(journal_);
      if (next < 0) {
        if (callbacks_.on_error) {
          callbacks_.on_error(QObject::tr("Failed to read the systemd journal."));
        }
        return;
      }
      if (next == 0) {
        break;
      }
      batch += currentEntryLine();
    }

    if (!batch.isEmpty() && callbacks_.on_bytes) {
      callbacks_.on_bytes(std::move(batch));
    }

    if (journal_ && drained_count == kMaxEntriesPerDrain) {
      drain_timer_.start(0);
    }
  }

  void processJournalChanges() {
    if (!journal_) {
      return;
    }

    const int process_result = sd_journal_process(journal_);
    if (process_result < 0) {
      if (callbacks_.on_error) {
        callbacks_.on_error(QObject::tr("Failed to read changes from the systemd journal."));
      }
      return;
    }
    drainAvailableEntries();
  }

  QTimer drain_timer_;
  QTimer journal_poll_timer_;
  std::unique_ptr<QSocketNotifier> notifier_;
  Callbacks callbacks_;
  sd_journal* journal_{nullptr};
  QString process_name_;
  QString start_mode_;
  mutable QHash<uint32_t, QByteArray> process_name_cache_;
};
#endif

}  // namespace

StreamSource::StreamSource() {
  flush_timer_.setSingleShot(true);
  flush_timer_.setInterval(40);
  QObject::connect(&flush_timer_, &QTimer::timeout, &flush_timer_, [this]() {
    flushPendingBytes();
  });
  catch_up_idle_timer_.setSingleShot(true);
  catch_up_idle_timer_.setInterval(250);
  QObject::connect(&catch_up_idle_timer_, &QTimer::timeout, &catch_up_idle_timer_, [this]() {
    completeInitialCatchUp();
  });
}

StreamSource::~StreamSource() {
  closeInternal(false);
}

bool StreamSource::isSupportedUrl(const QUrl& url) {
  const auto scheme = url.scheme();
  return scheme == QLatin1StringView{kPipeScheme}
      || scheme == QLatin1StringView{kDockerScheme}
      || scheme == QLatin1StringView{kAdbScheme}
      || scheme == QLatin1StringView{kSystemdScheme};
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

QUrl StreamSource::makeDockerUrl(const QString& container_id, const QString& container_name,
                                 bool no_history) {
  QUrl url;
  url.setScheme(QLatin1StringView{kDockerScheme});
  url.setPath(QStringLiteral("/") + QUuid::createUuid().toString(QUuid::WithoutBraces));

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("container"), container_id.trimmed());
  if (!container_name.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("name"), container_name.trimmed());
  }
  if (no_history) {
    query.addQueryItem(QStringLiteral("noHistory"), QStringLiteral("1"));
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
  spec.no_history = queryFlagValue(query, QStringLiteral("noHistory"), false);
  if (spec.container_id.isEmpty()) {
    return std::nullopt;
  }

  return spec;
}

QUrl StreamSource::makeAdbLogcatUrl(const QString& serial, const QString& name, bool no_history) {
  QUrl url;
  url.setScheme(QLatin1StringView{kAdbScheme});
  url.setPath(QStringLiteral("/") + QUuid::createUuid().toString(QUuid::WithoutBraces));

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("serial"), serial.trimmed());
  if (!name.trimmed().isEmpty()) {
    query.addQueryItem(QStringLiteral("name"), name.trimmed());
  }
  if (no_history) {
    query.addQueryItem(QStringLiteral("noHistory"), QStringLiteral("1"));
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
  spec.no_history = queryFlagValue(query, QStringLiteral("noHistory"), false);
  if (spec.serial.isEmpty()) {
    return std::nullopt;
  }

  return spec;
}

QUrl StreamSource::makeSystemdJournalUrl(const QString& process_name, const QString& start_mode) {
  QUrl url;
  url.setScheme(QLatin1StringView{kSystemdScheme});
  url.setPath(QStringLiteral("/") + QUuid::createUuid().toString(QUuid::WithoutBraces));

  const auto trimmed = process_name.trimmed();
  const auto mode = start_mode.trimmed();
  if (!trimmed.isEmpty() || !mode.isEmpty()) {
    QUrlQuery query;
    if (!trimmed.isEmpty()) {
      query.addQueryItem(QStringLiteral("process"), trimmed);
    }
    if (!mode.isEmpty()) {
      query.addQueryItem(QStringLiteral("start"), mode);
    }
    url.setQuery(query);
  }
  return url;
}

std::optional<SystemdJournalSpec> StreamSource::parseSystemdJournalSpec(const QUrl& url) {
  if (url.scheme() != QLatin1StringView{kSystemdScheme}) {
    return std::nullopt;
  }

  const QUrlQuery query(url);
  SystemdJournalSpec spec;
  spec.instance_id = url.path();
  if (spec.instance_id.startsWith(QLatin1Char('/'))) {
    spec.instance_id.remove(0, 1);
  }
  spec.process_name = query.queryItemValue(QStringLiteral("process")).trimmed();
  spec.start_mode = query.queryItemValue(QStringLiteral("start")).trimmed();
  return spec;
}

void StreamSource::setCallbacks(SourceCallbacks callbacks) {
  LogSource::setCallbacks(std::move(callbacks));
  spool_source_.setCallbacks(SourceCallbacks{
      .on_state_changed =
          [this](SourceSnapshot) {
            auto next_snapshot = snapshot();
            next_snapshot.following = following_;
            emitStateChanged(next_snapshot);
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
  if (url.scheme() == QLatin1StringView{kSystemdScheme}
      && spool_source_.requestedScannerName() == "Auto") {
    spool_source_.setRequestedScannerName("Systemd");
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
            if (catching_up_) {
              catch_up_idle_timer_.start();
            }
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
  catching_up_ = !sourceStartsLive(url);
  if (catching_up_) {
    catch_up_idle_timer_.start();
  }
  provider_->start();
  open_ = true;
}

void StreamSource::close() {
  closeInternal(true);
}

void StreamSource::closeInternal(bool invalidate_pages) {
  if (!open_ && !provider_ && spool_path_.isEmpty() && !spool_file_.isOpen()
      && pending_bytes_.isEmpty()) {
    return;
  }

  open_ = false;
  flush_timer_.stop();
  catch_up_idle_timer_.stop();
  catching_up_ = false;
  if (provider_) {
    provider_->setCallbacks({});
    provider_->stop();
  }

  pending_bytes_.clear();
  failed_ = false;
  provider_.reset();
  spool_source_.setCallbacks({});
  if (invalidate_pages) {
    spool_source_.close();
  }
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
  source_snapshot.catching_up = source_snapshot.catching_up || catching_up_;
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

std::vector<uint32_t> StreamSource::logcatPids() const {
  return spool_source_.logcatPids();
}

std::vector<std::string> StreamSource::systemdProcessNames() const {
  return spool_source_.systemdProcessNames();
}

std::shared_ptr<const SourceWindow> StreamSource::windowForSourceRange(uint64_t first_line,
                                                                       size_t count, bool raw) {
  return spool_source_.windowForSourceRange(first_line, count, raw);
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
    return std::make_unique<DockerStreamProvider>(docker_spec->container_id, docker_spec->no_history);
  }
  if (const auto adb_spec = parseAdbLogcatSpec(url); adb_spec.has_value()) {
    LOG_INFO << "Using adb logcat provider for serial='" << adb_spec->serial.toStdString() << "'";
    return std::make_unique<AdbLogcatProvider>(adb_spec->serial, adb_spec->no_history);
  }
  if (const auto systemd_spec = parseSystemdJournalSpec(url); systemd_spec.has_value()) {
#ifdef LGX_ENABLE_SYSTEMD_SOURCE
    LOG_INFO << "Using systemd journal provider";
    return std::make_unique<SystemdJournalProvider>(systemd_spec->process_name,
                                                    systemd_spec->start_mode);
#else
    LOG_WARN << "Systemd journal provider requested but LGX_ENABLE_SYSTEMD_SOURCE is disabled";
    return {};
#endif
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

void StreamSource::completeInitialCatchUp() {
  if (!catching_up_) {
    return;
  }

  catching_up_ = false;
  emitStateChanged(snapshot());
}

bool StreamSource::sourceStartsLive(const QUrl& url) const {
  if (url.scheme() == QLatin1StringView{kPipeScheme}) {
    return true;
  }

  if (const auto docker = parseDockerSpec(url); docker.has_value()) {
    return docker->no_history;
  }

  if (const auto adb = parseAdbLogcatSpec(url); adb.has_value()) {
    return adb->no_history;
  }

  if (const auto systemd = parseSystemdJournalSpec(url); systemd.has_value()) {
    return systemd->start_mode == QStringLiteral("now");
  }

  return false;
}

}  // namespace lgx
