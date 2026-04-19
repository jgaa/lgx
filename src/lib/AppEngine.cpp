#include "AppEngine.h"

#include <memory>

#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

#include "FilterModel.h"
#include "FileSource.h"
#include "LogScanner.h"
#include "MarkedModel.h"
#include "StreamSource.h"
#include "UiSettings.h"
#include "logging.h"
#include "util.h"

namespace lgx {
namespace {

constexpr int kRecentLogSourceLimit = 25;
constexpr int kLogSourceMetadataLimit = 100;
constexpr auto kRecentLogSourcesKey = "session/recentLogSources";
constexpr auto kRecentPipeStreamsKey = "session/recentPipeStreams";
constexpr auto kLogSourceMetadataKey = "session/logSourceMetadata";
constexpr auto kLogSourceMetadataUrlKey = "url";
constexpr auto kLogSourceMetadataScannerNameKey = "scannerName";
constexpr auto kLogSourceMetadataWrapLogLinesKey = "wrapLogLines";
constexpr auto kManagedCacheRootName = "lgx";

QString managedCacheRootPath() {
  QString cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cache_root.isEmpty()) {
    cache_root = QDir::tempPath();
  }

  return QDir(cache_root).filePath(QLatin1StringView{kManagedCacheRootName});
}

QUrl normalizeSourceUrl(const QUrl& url) {
  return url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
}

QVariantList sanitizedLogSourceMetadataEntries(const QVariant& value) {
  QVariantList sanitized;
  QSet<QString> seen_urls;
  const auto url_key = QString::fromLatin1(kLogSourceMetadataUrlKey);
  const auto scanner_name_key = QString::fromLatin1(kLogSourceMetadataScannerNameKey);
  const auto wrap_log_lines_key = QString::fromLatin1(kLogSourceMetadataWrapLogLinesKey);
  for (const auto& entry_value : value.toList()) {
    const auto entry = entry_value.toMap();
    const auto canonical_url = normalizeSourceUrl(QUrl(entry.value(url_key).toString()));
    const auto scanner_name = entry.value(scanner_name_key).toString().trimmed();
    const auto has_wrap_value = entry.contains(wrap_log_lines_key);
    const auto wrap_log_lines = entry.value(wrap_log_lines_key).toBool();
    const auto encoded_url = canonical_url.toString();
    if (!canonical_url.isValid() || canonical_url.isEmpty() || seen_urls.contains(encoded_url)
        || (scanner_name.isEmpty() && !has_wrap_value)) {
      continue;
    }

    seen_urls.insert(encoded_url);
    QVariantMap metadata_entry;
    metadata_entry.insert(url_key, encoded_url);
    if (!scanner_name.isEmpty()) {
      metadata_entry.insert(scanner_name_key, scanner_name);
    }
    if (has_wrap_value) {
      metadata_entry.insert(wrap_log_lines_key, wrap_log_lines);
    }
    sanitized.push_back(metadata_entry);
    if (sanitized.size() == kLogSourceMetadataLimit) {
      break;
    }
  }

  return sanitized;
}

QString adbProgramName() {
#ifdef Q_OS_WIN
  return QStringLiteral("adb.exe");
#else
  return QStringLiteral("adb");
#endif
}

bool isExecutableFile(const QString& path) {
  return isHostExecutableFile(path);
}

QString processLabel(const QString& name, int pid) {
  if (name.trimmed().isEmpty()) {
    return QObject::tr("PID %1").arg(pid);
  }

  return QObject::tr("%1 (%2)").arg(name.trimmed()).arg(pid);
}

QHash<int, QString> parseAdbProcessOutput(const QString& text) {
  QHash<int, QString> processes;
  const auto lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                Qt::SkipEmptyParts);
  if (lines.isEmpty()) {
    return processes;
  }

  auto parse_pid = [](QStringView token) -> int {
    bool ok = false;
    const int value = token.toInt(&ok);
    return ok ? value : -1;
  };

  auto insert_process = [&processes](int pid, QString name) {
    if (pid <= 0) {
      return;
    }
    name = name.trimmed();
    if (name.isEmpty()) {
      return;
    }
    processes.insert(pid, name);
  };

  const auto first_tokens = lines.front().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const int pid_header_index = first_tokens.indexOf(QStringLiteral("PID"));
  int name_header_index = first_tokens.indexOf(QStringLiteral("NAME"));
  if (name_header_index < 0) {
    name_header_index = first_tokens.indexOf(QStringLiteral("CMDLINE"));
  }
  if (name_header_index < 0) {
    name_header_index = first_tokens.indexOf(QStringLiteral("ARGS"));
  }

  if (pid_header_index >= 0) {
    for (int line_index = 1; line_index < lines.size(); ++line_index) {
      const auto tokens = lines.at(line_index).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
      if (tokens.size() <= pid_header_index) {
        continue;
      }

      const int pid = parse_pid(tokens.at(pid_header_index));
      if (pid <= 0) {
        continue;
      }

      QString name;
      if (name_header_index >= 0 && name_header_index < tokens.size()) {
        name = tokens.mid(name_header_index).join(QLatin1Char(' '));
      } else if (!tokens.isEmpty()) {
        name = tokens.back();
      }
      insert_process(pid, name);
    }
    return processes;
  }

  for (const auto& line : lines) {
    const auto tokens = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 2) {
      continue;
    }

    int pid = parse_pid(tokens.front());
    QString name;
    if (pid > 0) {
      name = tokens.mid(1).join(QLatin1Char(' '));
    } else {
      pid = parse_pid(tokens.value(1));
      if (pid > 0) {
        name = tokens.back();
      }
    }
    insert_process(pid, name);
  }

  return processes;
}

QHash<int, QString> queryAdbProcessesForSerial(const QString& adb_path, const QString& serial) {
  if (!isExecutableFile(adb_path) || serial.trimmed().isEmpty()) {
    return {};
  }

  auto run_query = [&adb_path, &serial](const QStringList& arguments) -> QString {
    QProcess process;
    configureHostProcess(process, adb_path, arguments);
    process.start();
    if (!process.waitForFinished(3000)) {
      process.kill();
      process.waitForFinished(200);
      return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
      return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
  };

  const auto table =
      run_query({QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("ps"),
                 QStringLiteral("-A"), QStringLiteral("-o"), QStringLiteral("PID,NAME")});
  if (!table.trimmed().isEmpty()) {
    return parseAdbProcessOutput(table);
  }

  const auto fallback =
      run_query({QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("ps"),
                 QStringLiteral("-A")});
  return parseAdbProcessOutput(fallback);
}

QString adbVersionText(const QString& executable_path) {
  if (!isExecutableFile(executable_path)) {
    return {};
  }

  QProcess process;
  configureHostProcess(process, executable_path, {QStringLiteral("version")});
  process.start();
  if (!process.waitForFinished(2000)) {
    process.kill();
    process.waitForFinished(200);
    return {};
  }

  const auto output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  const auto first_line = output.section(QLatin1Char('\n'), 0, 0).trimmed();
  return first_line;
}

QStringList candidateAdbRoots() {
  QStringList roots;
  const auto add_root = [&roots](const QString& value) {
    const auto trimmed = value.trimmed();
    if (!trimmed.isEmpty()) {
      roots.push_back(QDir::cleanPath(trimmed));
    }
  };

  add_root(qEnvironmentVariable("ANDROID_SDK_ROOT"));
  add_root(qEnvironmentVariable("ANDROID_HOME"));

  const auto home = QDir::homePath();
  add_root(QDir(home).filePath(QStringLiteral("Android/Sdk")));
  add_root(QDir(home).filePath(QStringLiteral("Android/sdk")));
  add_root(QDir(home).filePath(QStringLiteral("Library/Android/sdk")));

  roots.removeDuplicates();
  return roots;
}

QString adbCandidateFromRoot(const QString& root_path) {
  if (root_path.isEmpty()) {
    return {};
  }

  if (isExecutableFile(root_path)) {
    return QFileInfo(root_path).canonicalFilePath();
  }

  const QDir root_dir(root_path);
  const auto direct = root_dir.filePath(adbProgramName());
  if (isExecutableFile(direct)) {
    return QFileInfo(direct).canonicalFilePath();
  }

  const auto platform_tools = root_dir.filePath(QStringLiteral("platform-tools/%1").arg(adbProgramName()));
  if (isExecutableFile(platform_tools)) {
    return QFileInfo(platform_tools).canonicalFilePath();
  }

  QDirIterator it(root_path,
                  QStringList{adbProgramName()},
                  QDir::Files | QDir::Executable,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const auto candidate = QFileInfo(it.next()).canonicalFilePath();
    if (!candidate.isEmpty() && candidate.contains(QStringLiteral("/platform-tools/"))) {
      return candidate;
    }
  }

  return {};
}

QString adbDeviceLabel(const QString& model, const QString& device, const QString& serial) {
  if (!model.trimmed().isEmpty()) {
    return model.trimmed();
  }
  if (!device.trimmed().isEmpty()) {
    return device.trimmed();
  }
  return serial.trimmed();
}

QString normalizedRecentPipeKey(const QUrl& url) {
  const auto pipe_spec = StreamSource::parsePipeSpec(url);
  if (!pipe_spec.has_value()) {
    return {};
  }

  QUrl normalized;
  normalized.setScheme(QStringLiteral("pipe"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("cmd"), pipe_spec->command);
  query.addQueryItem(QStringLiteral("stdout"),
                     pipe_spec->capture_stdout ? QStringLiteral("1") : QStringLiteral("0"));
  query.addQueryItem(QStringLiteral("stderr"),
                     pipe_spec->capture_stderr ? QStringLiteral("1") : QStringLiteral("0"));
  normalized.setQuery(query);
  return normalized.toString();
}

QString commandDisplayName(const QString& command) {
  const auto trimmed = command.trimmed();
  if (trimmed.isEmpty()) {
    return QObject::tr("Pipe");
  }

  qsizetype split_at = trimmed.indexOf(QRegularExpression(QStringLiteral("\\s")));
  if (split_at < 0) {
    split_at = trimmed.size();
  }

  QString token = trimmed.left(split_at).trimmed();
  if ((token.startsWith(QLatin1Char('"')) && token.endsWith(QLatin1Char('"')))
      || (token.startsWith(QLatin1Char('\'')) && token.endsWith(QLatin1Char('\'')))) {
    token = token.sliced(1, std::max<qsizetype>(0, token.size() - 2));
  }

  const QFileInfo info(token);
  const auto file_name = info.fileName();
  return file_name.isEmpty() ? token : file_name;
}

}  // namespace

AppEngine::AppEngine(QObject* parent)
    : QObject(parent) {
  if (auto* app = QCoreApplication::instance()) {
    app->installEventFilter(this);
  }
  docker_executable_ = findHostExecutable(QStringLiteral("docker"));
  connect(&UiSettings::instance(), &UiSettings::adbExecutablePathChanged, this, [this]() {
    emit adbExecutablePathChanged();
    emit adbAvailabilityChanged();
  });
  connect(&open_logs_, &QAbstractItemModel::rowsInserted, this, &AppEngine::openLogCountChanged);
  connect(&open_logs_, &QAbstractItemModel::rowsRemoved, this, &AppEngine::openLogCountChanged);
  connect(&open_logs_, &QAbstractItemModel::modelReset, this, &AppEngine::openLogCountChanged);
  connect(&open_logs_, &QAbstractItemModel::rowsInserted, this, &AppEngine::openStreamCountChanged);
  connect(&open_logs_, &QAbstractItemModel::rowsRemoved, this, &AppEngine::openStreamCountChanged);
  connect(&open_logs_, &QAbstractItemModel::modelReset, this, &AppEngine::openStreamCountChanged);
  connect(&open_logs_, &QAbstractItemModel::rowsInserted, this, [this]() { updateCurrentLogModel(); });
  connect(&open_logs_, &QAbstractItemModel::rowsRemoved, this, [this]() { updateCurrentLogModel(); });
  connect(&open_logs_, &QAbstractItemModel::modelReset, this, [this]() { updateCurrentLogModel(); });
  loadRecentLogSources();
  loadRecentPipeStreams();
}

AppEngine::~AppEngine() {
  current_log_model_ = nullptr;

  const auto filter_models = findChildren<FilterModel*>(QString{}, Qt::FindDirectChildrenOnly);
  for (auto* model : filter_models) {
    delete model;
  }

  const auto marked_models = findChildren<MarkedModel*>(QString{}, Qt::FindDirectChildrenOnly);
  for (auto* model : marked_models) {
    delete model;
  }

  const auto model_entries = std::move(models_);
  models_.clear();
  for (const auto& entry : model_entries) {
    if (entry.model) {
      delete entry.model;
    }
  }
}

AppEngine& AppEngine::instance() {
  static AppEngine instance;
  return instance;
}

QAbstractItemModel* AppEngine::openLogs() noexcept {
  return &open_logs_;
}

int AppEngine::openLogCount() const noexcept {
  return open_logs_.rowCount();
}

int AppEngine::openStreamCount() const noexcept {
  int count = 0;
  for (int index = 0; index < open_logs_.rowCount(); ++index) {
    if (StreamSource::isSupportedUrl(open_logs_.sourceUrlAt(index))) {
      ++count;
    }
  }

  return count;
}

QAbstractItemModel* AppEngine::recentLogs() noexcept {
  return &recent_logs_;
}

int AppEngine::recentLogCount() const noexcept {
  return recent_logs_.rowCount();
}

QAbstractItemModel* AppEngine::recentPipeStreams() noexcept {
  return &recent_pipe_streams_;
}

int AppEngine::recentPipeStreamCount() const noexcept {
  return recent_pipe_streams_.rowCount();
}

bool AppEngine::dockerAvailable() const noexcept {
  return !docker_executable_.isEmpty();
}

bool AppEngine::systemdAvailable() const noexcept {
#ifdef LGX_ENABLE_SYSTEMD_SOURCE
  return !isRunningInFlatpak() || canSpawnHostCommands();
#else
  return false;
#endif
}

QString AppEngine::adbExecutablePath() const noexcept {
  return UiSettings::instance().adbExecutablePath();
}

bool AppEngine::adbAvailable() const noexcept {
  return isExecutableFile(adbExecutablePath());
}

QAbstractItemModel* AppEngine::adbCandidates() noexcept {
  return &adb_candidates_;
}

int AppEngine::adbCandidateCount() const noexcept {
  return adb_candidates_.rowCount();
}

QString AppEngine::adbScanError() const noexcept {
  return adb_scan_error_;
}

QAbstractItemModel* AppEngine::adbDevices() noexcept {
  return &adb_devices_;
}

int AppEngine::adbDeviceCount() const noexcept {
  return adb_devices_.rowCount();
}

QString AppEngine::adbDeviceQueryError() const noexcept {
  return adb_device_query_error_;
}

QAbstractItemModel* AppEngine::dockerContainers() noexcept {
  return &docker_containers_;
}

int AppEngine::dockerContainerCount() const noexcept {
  return docker_containers_.rowCount();
}

QString AppEngine::dockerContainerQueryError() const noexcept {
  return docker_container_query_error_;
}

QStringList AppEngine::logScanners() const {
  return availableLogScannerNames();
}

int AppEngine::currentOpenLogIndex() const noexcept {
  return current_open_log_index_;
}

QUrl AppEngine::currentOpenLogSourceUrl() const {
  return current_open_log_source_url_;
}

QObject* AppEngine::currentLogModel() const noexcept {
  return current_log_model_;
}

void AppEngine::setAdbExecutablePath(const QString& path) {
  const auto previous_available = adbAvailable();
  UiSettings::instance().setAdbExecutablePath(path);
  if (previous_available != adbAvailable()) {
    emit adbAvailabilityChanged();
  }
}

void AppEngine::setCurrentOpenLogIndex(int index) {
  const int normalized_index =
      (index >= 0 && index < open_logs_.rowCount()) ? index : -1;
  if (current_open_log_index_ == normalized_index) {
    updateCurrentLogModel();
    return;
  }

  current_open_log_index_ = normalized_index;
  emit currentOpenLogIndexChanged();
  updateCurrentLogModel();
}

int AppEngine::openLogSource(const QUrl& url) {
  return openLogSourceInternal(url, true);
}

int AppEngine::openPipeStream(const QString& command, bool include_stdout, bool include_stderr,
                              bool remember_in_recents) {
  if (command.trimmed().isEmpty() || (!include_stdout && !include_stderr)) {
    return -1;
  }

  const auto url = StreamSource::makePipeUrl(command, include_stdout, include_stderr);
  if (remember_in_recents) {
    addRecentPipeStream(url);
  } else {
    removeRecentPipeStream(url);
  }
  return openLogSourceInternal(url, false);
}

int AppEngine::openDockerContainerStream(const QString& container_id,
                                         const QString& container_name,
                                         bool no_history) {
  LOG_INFO << "Request to open Docker stream for container='"
           << container_id.trimmed().toStdString()
           << "' name='" << container_name.trimmed().toStdString()
           << "' noHistory=" << (no_history ? "true" : "false");
  if (!dockerAvailable() || container_id.trimmed().isEmpty()) {
    LOG_WARN << "Rejecting Docker stream open request. dockerAvailable="
             << (dockerAvailable() ? "true" : "false")
             << " containerIdEmpty=" << (container_id.trimmed().isEmpty() ? "true" : "false");
    return -1;
  }

  const auto url = StreamSource::makeDockerUrl(container_id.trimmed(), container_name.trimmed(), no_history);
  LOG_INFO << "Created Docker stream URL '" << url.toString().toStdString() << "'";
  const auto index = openLogSourceInternal(url, false);
  LOG_INFO << "Open Docker stream resolved to tab index=" << index;
  return index;
}

int AppEngine::openAdbLogcatStream(const QString& serial, const QString& name, bool no_history) {
  LOG_INFO << "Request to open ADB logcat stream for serial='"
           << serial.trimmed().toStdString()
           << "' name='" << name.trimmed().toStdString()
           << "' noHistory=" << (no_history ? "true" : "false");
  if (!adbAvailable() || serial.trimmed().isEmpty()) {
    LOG_WARN << "Rejecting ADB logcat open request. adbAvailable="
             << (adbAvailable() ? "true" : "false")
             << " serialEmpty=" << (serial.trimmed().isEmpty() ? "true" : "false");
    return -1;
  }

  const auto url = StreamSource::makeAdbLogcatUrl(serial.trimmed(), name.trimmed(), no_history);
  LOG_INFO << "Created ADB logcat URL: " << url.toString().toStdString();
  const auto index = openLogSourceInternal(url, false);
  LOG_INFO << "ADB logcat open result index=" << index;
  return index;
}

int AppEngine::openSystemdJournalStream(const QString& process_name, const QString& start_mode) {
  LOG_INFO << "Request to open systemd journal stream for process='"
           << process_name.trimmed().toStdString() << "' start_mode='"
           << start_mode.trimmed().toStdString() << "'";
  if (!systemdAvailable()) {
    LOG_WARN << "Rejecting systemd journal open request because support is disabled";
    return -1;
  }

  const auto url = StreamSource::makeSystemdJournalUrl(process_name.trimmed(), start_mode.trimmed());
  LOG_INFO << "Created systemd journal URL: " << url.toString().toStdString();
  const auto index = openLogSourceInternal(url, false);
  LOG_INFO << "Systemd journal open result index=" << index;
  return index;
}

int AppEngine::scanAdbExecutables() {
  std::vector<QStringList> entries;
  QSet<QString> seen_paths;

  const auto add_candidate = [&entries, &seen_paths](const QString& path, const QString& source) {
    const auto canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty() || seen_paths.contains(canonical)) {
      return;
    }

    seen_paths.insert(canonical);
    entries.push_back(QStringList{
        canonical,
        adbVersionText(canonical),
        source,
    });
  };

  const auto from_path = findHostExecutable(adbProgramName());
  if (!from_path.isEmpty()) {
    add_candidate(from_path, tr("PATH"));
  }

  for (const auto& root : candidateAdbRoots()) {
    const auto candidate = adbCandidateFromRoot(root);
    if (!candidate.isEmpty()) {
      add_candidate(candidate, root);
    }
  }

  std::sort(entries.begin(), entries.end(), [](const QStringList& lhs, const QStringList& rhs) {
    return lhs.value(0) < rhs.value(0);
  });

  adb_candidates_.setEntries(entries);
  emit adbCandidatesChanged();

  const QString next_error = entries.empty() ? tr("No adb executable found.") : QString{};
  if (adb_scan_error_ != next_error) {
    adb_scan_error_ = next_error;
    emit adbScanErrorChanged();
  }

  if (entries.size() == 1) {
    setAdbExecutablePath(entries.front().value(0));
  }

  return static_cast<int>(entries.size());
}

bool AppEngine::refreshAdbDevices() {
  if (!adbAvailable()) {
    adb_devices_.setEntries({});
    const auto next_error = tr("ADB executable path is not configured.");
    if (adb_device_query_error_ != next_error) {
      adb_device_query_error_ = next_error;
      emit adbDeviceQueryErrorChanged();
    }
    emit adbDevicesChanged();
    return false;
  }

  QProcess process;
  configureHostProcess(process, adbExecutablePath(), {QStringLiteral("devices"), QStringLiteral("-l")});
  process.start();
  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished(500);
    adb_devices_.setEntries({});
    const auto next_error = tr("Timed out while querying ADB devices.");
    if (adb_device_query_error_ != next_error) {
      adb_device_query_error_ = next_error;
      emit adbDeviceQueryErrorChanged();
    }
    emit adbDevicesChanged();
    return false;
  }

  const auto stderr_text = QString::fromUtf8(process.readAllStandardError()).trimmed();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    adb_devices_.setEntries({});
    const auto next_error = !stderr_text.isEmpty() ? stderr_text : tr("Failed to query ADB devices.");
    if (adb_device_query_error_ != next_error) {
      adb_device_query_error_ = next_error;
      emit adbDeviceQueryErrorChanged();
    }
    emit adbDevicesChanged();
    return false;
  }

  std::vector<QStringList> entries;
  const auto lines = QString::fromUtf8(process.readAllStandardOutput())
                         .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line.startsWith(QStringLiteral("List of devices attached"))) {
      continue;
    }

    const auto tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.size() < 2) {
      continue;
    }

    QString model;
    QString device;
    QString product;
    QString transport_id;
    for (qsizetype i = 2; i < tokens.size(); ++i) {
      const auto& token = tokens.at(i);
      if (token.startsWith(QStringLiteral("model:"))) {
        model = token.sliced(6);
      } else if (token.startsWith(QStringLiteral("device:"))) {
        device = token.sliced(7);
      } else if (token.startsWith(QStringLiteral("product:"))) {
        product = token.sliced(8);
      } else if (token.startsWith(QStringLiteral("transport_id:"))) {
        transport_id = token.sliced(13);
      }
    }

    entries.push_back(QStringList{
        tokens.at(0),
        tokens.at(1),
        model,
        device,
        product,
        transport_id,
    });
  }

  adb_devices_.setEntries(std::move(entries));
  LOG_INFO << "ADB device query returned " << adb_devices_.rowCount() << " device(s)";
  if (!adb_device_query_error_.isEmpty()) {
    adb_device_query_error_.clear();
    emit adbDeviceQueryErrorChanged();
  }
  emit adbDevicesChanged();
  return true;
}

bool AppEngine::refreshDockerContainers() {
  if (!dockerAvailable()) {
    docker_containers_.setEntries({});
    const QString next_error = tr("Docker is not installed.");
    if (docker_container_query_error_ != next_error) {
      docker_container_query_error_ = next_error;
      emit dockerContainerQueryErrorChanged();
    }
    emit dockerContainersChanged();
    return false;
  }

  QProcess process;
  configureHostProcess(process,
                       docker_executable_,
                       {QStringLiteral("ps"),
                        QStringLiteral("--format"),
                        QStringLiteral("{{.ID}}\t{{.Names}}\t{{.Image}}\t{{.Status}}")});
  process.start();
  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished(500);
    docker_containers_.setEntries({});
    const QString next_error = tr("Timed out while querying Docker containers.");
    if (docker_container_query_error_ != next_error) {
      docker_container_query_error_ = next_error;
      emit dockerContainerQueryErrorChanged();
    }
    emit dockerContainersChanged();
    return false;
  }

  const QString stderr_text = QString::fromUtf8(process.readAllStandardError()).trimmed();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    docker_containers_.setEntries({});
    const QString next_error = !stderr_text.isEmpty()
        ? stderr_text
        : tr("Failed to query Docker containers.");
    if (docker_container_query_error_ != next_error) {
      docker_container_query_error_ = next_error;
      emit dockerContainerQueryErrorChanged();
    }
    emit dockerContainersChanged();
    return false;
  }

  std::vector<QStringList> entries;
  const auto lines = QString::fromUtf8(process.readAllStandardOutput())
                         .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  entries.reserve(lines.size());
  for (const auto& line : lines) {
    const auto fields = line.split(QLatin1Char('\t'));
    if (!fields.isEmpty() && !fields.first().trimmed().isEmpty()) {
      entries.push_back(fields);
    }
  }

  docker_containers_.setEntries(std::move(entries));
  if (!docker_container_query_error_.isEmpty()) {
    docker_container_query_error_.clear();
    emit dockerContainerQueryErrorChanged();
  }
  emit dockerContainersChanged();
  return true;
}

int AppEngine::openLogSourceInternal(const QUrl& url, bool add_to_recent) {
  const auto canonical = canonicalUrl(url);
  LOG_INFO << "Open log source request url='" << url.toString().toStdString()
           << "' canonical='" << canonical.toString().toStdString()
           << "' addToRecent=" << (add_to_recent ? "true" : "false");
  if (!canonical.isValid() || canonical.isEmpty()) {
    LOG_WARN << "Rejecting log source open request because canonical URL is invalid or empty";
    return -1;
  }

  if (add_to_recent && canonical.isLocalFile()) {
    addRecentLogSource(canonical);
  }

  const auto index = open_logs_.addOpenLog(canonical, titleForUrl(canonical));
  LOG_INFO << "Open log source resolved to tab index=" << index;
  return index;
}

int AppEngine::openLogFile(const QUrl& initial_url) {
  QUrl initial_directory;
  const auto canonical_initial = canonicalUrl(initial_url);
  if (canonical_initial.isLocalFile()) {
    const QFileInfo file_info(canonical_initial.toLocalFile());
    const auto absolute_directory = file_info.absoluteDir().absolutePath();
    if (!absolute_directory.isEmpty()) {
      initial_directory = QUrl::fromLocalFile(absolute_directory);
    }
  }

  const auto selected = QFileDialog::getOpenFileUrl(
      nullptr,
      tr("Open Log File"),
      initial_directory,
      tr("Log files (*.log *.txt);;All files (*)"));
  if (!selected.isValid() || selected.isEmpty()) {
    return -1;
  }

  return openLogSource(selected);
}

int AppEngine::openRecentLogSourceAt(int index) {
  return openLogSource(recent_logs_.sourceUrlAt(index));
}

int AppEngine::openRecentPipeStreamAt(int index) {
  const auto recent_url = recent_pipe_streams_.sourceUrlAt(index);
  const auto pipe_spec = StreamSource::parsePipeSpec(recent_url);
  if (!pipe_spec.has_value()) {
    return -1;
  }

  return openPipeStream(
      pipe_spec->command,
      pipe_spec->capture_stdout,
      pipe_spec->capture_stderr,
      true);
}

bool AppEngine::closeOpenLogAt(int index) {
  const auto source_url = open_logs_.sourceUrlAt(index);
  if (!open_logs_.removeOpenLogAt(index)) {
    return false;
  }

  releaseLogModel(source_url);
  return true;
}

QUrl AppEngine::openLogSourceUrlAt(int index) const {
  return open_logs_.sourceUrlAt(index);
}

QString AppEngine::displaySourceTextForUrl(const QUrl& url) const {
  return displaySourceForUrl(canonicalUrl(url));
}

void AppEngine::copyTextToClipboard(const QString& text) const {
  if (auto* clipboard = QGuiApplication::clipboard()) {
    clipboard->setText(text);
  }
}

void AppEngine::logUiTrace(const QString& message) const {
  LOG_INFO << "UI: " << message.toStdString();
}

int AppEngine::activeLineMarkColor() const noexcept {
  const auto log_active_mark_color = [this](const int color) {
    QStringList pressed_keys;
    pressed_keys.reserve(pressed_keys_.size());
    for (const int key : pressed_keys_) {
      pressed_keys.push_back(QKeySequence(key).toString());
    }

    LOG_TRACE_N << "activeLineMarkColor pressedKeys=[" << pressed_keys.join(QStringLiteral(", ")).toStdString()
                << "] color=" << color;
    return color;
  };

  if (pressed_keys_.contains(Qt::Key_6)) {
    return log_active_mark_color(static_cast<int>(LineMark_Accent6));
  }
  if (pressed_keys_.contains(Qt::Key_5)) {
    return log_active_mark_color(static_cast<int>(LineMark_Accent5));
  }
  if (pressed_keys_.contains(Qt::Key_4)) {
    return log_active_mark_color(static_cast<int>(LineMark_Accent4));
  }
  if (pressed_keys_.contains(Qt::Key_3)) {
    return log_active_mark_color(static_cast<int>(LineMark_Accent3));
  }
  if (pressed_keys_.contains(Qt::Key_2)) {
    return log_active_mark_color(static_cast<int>(LineMark_Accent2));
  }
  if (pressed_keys_.contains(Qt::Key_1)) {
    return log_active_mark_color(static_cast<int>(LineMark_Accent1));
  }

  return log_active_mark_color(static_cast<int>(LineMark_Default));
}

bool AppEngine::cleanCache() {
  if (openStreamCount() > 0) {
    LOG_WARN << "Refusing to clean cache while stream-backed sources are open";
    return false;
  }

  const auto cache_root = managedCacheRootPath();
  if (cache_root.isEmpty()) {
    LOG_WARN << "Refusing to clean cache because managed cache root path is empty";
    return false;
  }

  QDir cache_dir(cache_root);
  if (!cache_dir.exists()) {
    return true;
  }

  const bool removed = cache_dir.removeRecursively();
  if (!removed) {
    LOG_WARN << "Failed to remove managed cache root '" << cache_root.toStdString() << "'";
  }
  return removed;
}

QVariantList AppEngine::logcatProcessesForSource(const QUrl& url) const {
  QVariantList entries;
  QVariantMap all_entry;
  all_entry.insert(QStringLiteral("pid"), 0);
  all_entry.insert(QStringLiteral("name"), QString{});
  all_entry.insert(QStringLiteral("label"), tr("All processes"));
  entries.push_back(all_entry);

  const auto canonical = canonicalUrl(url);
  auto* model = modelForUrl(canonical);
  if (!model || model->scannerName() != QStringLiteral("Logcat")) {
    return entries;
  }

  QSet<int> active_pids;
  if (auto* source = model->source()) {
    const auto source_pids = source->logcatPids();
    for (const auto pid : source_pids) {
      if (pid > 0) {
        active_pids.insert(static_cast<int>(pid));
      }
    }
  } else {
    for (int row = 0; row < model->rowCount(); ++row) {
      const int pid = model->pidAt(row);
      if (pid > 0) {
        active_pids.insert(pid);
      }
    }
  }

  if (active_pids.isEmpty()) {
    return entries;
  }

  QHash<int, QString> names;
  if (const auto adb_spec = StreamSource::parseAdbLogcatSpec(canonical); adb_spec.has_value()) {
    names = queryAdbProcessesForSerial(adbExecutablePath(), adb_spec->serial);
  }

  struct ProcessEntry {
    int pid{};
    QString name;
    QString label;
  };

  std::vector<ProcessEntry> sorted_entries;
  sorted_entries.reserve(static_cast<size_t>(active_pids.size()));
  for (const int pid : active_pids) {
    const auto name = names.value(pid).trimmed();
    sorted_entries.push_back(ProcessEntry{
        .pid = pid,
        .name = name,
        .label = processLabel(name, pid),
    });
  }

  std::sort(sorted_entries.begin(), sorted_entries.end(),
            [](const ProcessEntry& lhs, const ProcessEntry& rhs) {
              const bool lhs_named = !lhs.name.isEmpty();
              const bool rhs_named = !rhs.name.isEmpty();
              if (lhs_named != rhs_named) {
                return lhs_named;
              }

              if (lhs_named) {
                const int compare = QString::compare(lhs.name, rhs.name, Qt::CaseInsensitive);
                if (compare != 0) {
                  return compare < 0;
                }
              }

              return lhs.pid < rhs.pid;
            });

  for (const auto& process : sorted_entries) {
    QVariantMap entry;
    entry.insert(QStringLiteral("pid"), process.pid);
    entry.insert(QStringLiteral("name"), process.name);
    entry.insert(QStringLiteral("label"), process.label);
    entries.push_back(entry);
  }

  return entries;
}

QVariantList AppEngine::systemdProcessesForSource(const QUrl& url) const {
  QVariantList entries;
  QVariantMap all_entry;
  all_entry.insert(QStringLiteral("pid"), 0);
  all_entry.insert(QStringLiteral("name"), QString{});
  all_entry.insert(QStringLiteral("label"), tr("All processes"));
  entries.push_back(all_entry);

  const auto canonical = canonicalUrl(url);
  auto* model = modelForUrl(canonical);
  if (!model) {
    return entries;
  }
  const bool systemd_scanner =
      model->scannerName() == QStringLiteral("Systemd")
      || model->requestedScannerName() == QStringLiteral("Systemd");
  if (!systemd_scanner) {
    return entries;
  }

  QSet<QString> active_processes;
  if (auto* source = model->source()) {
    const auto process_names = source->systemdProcessNames();
    for (const auto& name : process_names) {
      const auto trimmed = QString::fromStdString(name).trimmed();
      if (!trimmed.isEmpty()) {
        active_processes.insert(trimmed);
      }
    }
  } else {
    for (int row = 0; row < model->rowCount(); ++row) {
      const auto name = model->functionNameAt(row).trimmed();
      if (!name.isEmpty()) {
        active_processes.insert(name);
      }
    }
  }

  QStringList names;
  names.reserve(active_processes.size());
  for (const auto& name : active_processes) {
    names.push_back(name);
  }
  names.sort(Qt::CaseInsensitive);
  for (const auto& name : names) {
    QVariantMap entry;
    entry.insert(QStringLiteral("pid"), 0);
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("label"), name);
    entries.push_back(entry);
  }

  return entries;
}

bool AppEngine::wrapLogLinesForSource(const QUrl& url) const {
  bool found = false;
  const bool saved_value = savedLogSourceWrapLogLines(url, &found);
  return found ? saved_value : UiSettings::instance().wrapLogLinesByDefault();
}

void AppEngine::saveWrapLogLinesForSource(const QUrl& url, bool enabled) const {
  const auto canonical = canonicalUrl(url);
  if (!canonical.isValid() || canonical.isEmpty()) {
    return;
  }

  QString scanner_name;
  if (auto* model = modelForUrl(canonical)) {
    scanner_name = model->requestedScannerName().trimmed();
    if (scanner_name.isEmpty()) {
      scanner_name = model->scannerName().trimmed();
    }
  }
  if (scanner_name.isEmpty()) {
    scanner_name = savedLogSourceScannerName(canonical);
  }
  if (scanner_name.isEmpty()) {
    scanner_name = UiSettings::instance().defaultLogScannerName();
  }

  saveLogSourceMetadata(canonical, scanner_name, enabled);
}

QObject* AppEngine::createLogModel(const QUrl& url) {
  const auto canonical = canonicalUrl(url);
  LOG_INFO << "Create log model request for '" << canonical.toString().toStdString() << "'";
  if (!canonical.isValid() || canonical.isEmpty()) {
    LOG_WARN << "Rejecting log model creation because URL is invalid or empty";
    return nullptr;
  }

  auto it = models_.find(canonical);
  if (it != models_.end() && it->model) {
    ++it->retain_count;
    LOG_INFO << "Reusing existing log model. retainCount=" << it->retain_count;
    return it->model;
  }

  auto* model = new LogModel(canonical, this);
  QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
  model->setCurrent(canonical == current_open_log_source_url_);
  connect(model, &LogModel::scannerNameChanged, this, [this, model, canonical]() {
    const auto scanner_name = model->scannerName().trimmed();
    if (!scanner_name.isEmpty()) {
      saveLogSourceMetadata(canonical, scanner_name);
    }
  });

  if (canonical.isLocalFile()) {
    auto source = std::make_unique<FileSource>();
    if (const auto saved_scanner_name = savedLogSourceScannerName(canonical);
        !saved_scanner_name.isEmpty()) {
      source->setRequestedScannerName(saved_scanner_name.toStdString());
    } else {
      source->setRequestedScannerName(UiSettings::instance().defaultLogScannerName().toStdString());
    }
    source->open(canonical.toLocalFile().toStdString());
    model->setSource(std::move(source));
    model->setFollowing(UiSettings::instance().followLiveLogsByDefault());
    model->loadFromSource();
  } else if (StreamSource::isSupportedUrl(canonical)) {
    LOG_INFO << "Creating stream-backed log model for '" << canonical.toString().toStdString() << "'";
    auto source = std::make_unique<StreamSource>();
    if (canonical.scheme() == QStringLiteral("adb")) {
      source->setRequestedScannerName("Logcat");
    } else if (canonical.scheme() == QStringLiteral("systemd")) {
      source->setRequestedScannerName("Systemd");
    } else if (const auto saved_scanner_name = savedLogSourceScannerName(canonical);
               !saved_scanner_name.isEmpty()) {
      source->setRequestedScannerName(saved_scanner_name.toStdString());
    } else {
      source->setRequestedScannerName(UiSettings::instance().defaultLogScannerName().toStdString());
    }
    source->open(canonical.toString().toStdString());
    model->setSource(std::move(source));
    model->setFollowing(UiSettings::instance().followLiveLogsByDefault());
    model->loadFromSource();
  } else {
    LOG_WARN << "Marking model as failed because source scheme is unsupported: "
             << canonical.scheme().toStdString();
    model->markFailed();
  }

  models_.insert(canonical, ModelEntry{.model = model, .retain_count = 1});
  LOG_INFO << "Created new log model for '" << canonical.toString().toStdString() << "'";
  return model;
}

QObject* AppEngine::createFilterModel(const QUrl& url) {
  auto* source_model = qobject_cast<LogModel*>(createLogModel(url));
  if (!source_model) {
    return nullptr;
  }

  auto* model = new FilterModel(source_model, this);
  QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
  return model;
}

QObject* AppEngine::createMarkedModel(const QUrl& url) {
  auto* source_model = qobject_cast<LogModel*>(createLogModel(url));
  if (!source_model) {
    return nullptr;
  }

  auto* model = new MarkedModel(source_model, this);
  QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
  return model;
}

void AppEngine::releaseLogModel(const QUrl& url) {
  const auto canonical = canonicalUrl(url);
  auto it = models_.find(canonical);
  if (it == models_.end()) {
    return;
  }

  if (it->retain_count > 0) {
    --it->retain_count;
  }

  if (it->retain_count == 0) {
    if (it->model) {
      it->model->deleteLater();
    }
    models_.erase(it);
  }
}

void AppEngine::releaseFilterModel(QObject* model) {
  auto* filter_model = qobject_cast<FilterModel*>(model);
  if (!filter_model) {
    return;
  }

  const QUrl source_url = filter_model->sourceUrl();
  filter_model->prepareForRelease();
  filter_model->deleteLater();
  releaseLogModel(source_url);
}

void AppEngine::releaseMarkedModel(QObject* model) {
  auto* marked_model = qobject_cast<MarkedModel*>(model);
  if (!marked_model) {
    return;
  }

  const QUrl source_url = marked_model->sourceUrl();
  marked_model->deleteLater();
  releaseLogModel(source_url);
}

LogModel* AppEngine::modelForUrl(const QUrl& url) const {
  const auto canonical = canonicalUrl(url);
  const auto it = models_.find(canonical);
  if (it == models_.end()) {
    return nullptr;
  }

  return it->model;
}

int AppEngine::retainCountForUrl(const QUrl& url) const {
  const auto canonical = canonicalUrl(url);
  const auto it = models_.find(canonical);
  if (it == models_.end()) {
    return 0;
  }

  return it->retain_count;
}

void AppEngine::updateCurrentLogModel() {
  const QUrl next_source_url =
      (current_open_log_index_ >= 0 && current_open_log_index_ < open_logs_.rowCount())
          ? open_logs_.sourceUrlAt(current_open_log_index_)
          : QUrl{};

  const auto canonical_next_source_url = canonicalUrl(next_source_url);
  if (current_open_log_source_url_ == canonical_next_source_url && current_log_model_) {
    current_log_model_->setCurrent(!canonical_next_source_url.isEmpty());
    return;
  }

  const auto previous_source_url = current_open_log_source_url_;
  auto* previous_model = current_log_model_.data();

  current_open_log_source_url_ = canonical_next_source_url;
  current_log_model_ = canonical_next_source_url.isEmpty()
      ? nullptr
      : qobject_cast<LogModel*>(createLogModel(canonical_next_source_url));
  if (previous_model && previous_model != current_log_model_) {
    previous_model->setCurrent(false);
  }
  if (current_log_model_) {
    current_log_model_->setCurrent(true);
  }

  if (previous_source_url.isValid() && !previous_source_url.isEmpty()) {
    releaseLogModel(previous_source_url);
  }

  if (previous_model != current_log_model_) {
    emit currentLogModelChanged();
  }
  if (previous_source_url != current_open_log_source_url_) {
    emit currentOpenLogSourceUrlChanged();
  }
}

bool AppEngine::eventFilter(QObject* watched, QEvent* event) {
  switch (event->type()) {
    case QEvent::KeyPress: {
      const auto* key_event = static_cast<QKeyEvent*>(event);
      if (!key_event->isAutoRepeat()) {
        pressed_keys_.insert(key_event->key());
        LOG_TRACE_N << "AppEngine key press key=" << key_event->key()
                    << " text='" << key_event->text().toStdString()
                    << "' sequence='" << QKeySequence(key_event->key()).toString().toStdString() << "'";
      }
      break;
    }
    case QEvent::KeyRelease: {
      const auto* key_event = static_cast<QKeyEvent*>(event);
      if (!key_event->isAutoRepeat()) {
        pressed_keys_.remove(key_event->key());
        LOG_TRACE_N << "AppEngine key release key=" << key_event->key()
                    << " text='" << key_event->text().toStdString()
                    << "' sequence='" << QKeySequence(key_event->key()).toString().toStdString() << "'";
      }
      break;
    }
    case QEvent::ApplicationDeactivate:
      if (!pressed_keys_.isEmpty()) {
        LOG_TRACE_N << "AppEngine clearing pressed keys on event type=" << static_cast<int>(event->type());
      }
      pressed_keys_.clear();
      break;
    default:
      break;
  }

  return QObject::eventFilter(watched, event);
}

void AppEngine::saveSessionState() const {
  QStringList open_log_sources;
  open_log_sources.reserve(open_logs_.rowCount());
  for (int index = 0; index < open_logs_.rowCount(); ++index) {
    const auto url = open_logs_.sourceUrlAt(index);
    if (url.isValid() && !url.isEmpty() && url.isLocalFile()) {
      open_log_sources.push_back(url.toString());
    }
  }

  QSettings settings;
  settings.setValue("session/openLogSources", open_log_sources);
  settings.sync();
}

void AppEngine::restoreSavedSession() {
  if (!QSettings{}.value("startup/restoreOpenLogsOnStartup", false).toBool()) {
    return;
  }

  const auto values = QSettings{}.value("session/openLogSources").toStringList();
  for (const auto& value : values) {
    openLogSourceInternal(QUrl(value), false);
  }
}

QString AppEngine::titleForUrl(const QUrl& url) {
  if (const auto pipe_spec = StreamSource::parsePipeSpec(url); pipe_spec.has_value()) {
    return QObject::tr("Pipe: %1").arg(commandDisplayName(pipe_spec->command));
  }
  if (const auto docker_spec = StreamSource::parseDockerSpec(url); docker_spec.has_value()) {
    const auto label = docker_spec->container_name.isEmpty()
        ? docker_spec->container_id
        : docker_spec->container_name;
    return docker_spec->no_history
        ? QObject::tr("Docker: %1 (from now)").arg(label)
        : QObject::tr("Docker: %1").arg(label);
  }
  if (const auto adb_spec = StreamSource::parseAdbLogcatSpec(url); adb_spec.has_value()) {
    const auto label = adb_spec->name.isEmpty() ? adb_spec->serial : adb_spec->name;
    return adb_spec->no_history
        ? QObject::tr("Logcat: %1 (from now)").arg(label)
        : QObject::tr("Logcat: %1").arg(label);
  }
  if (const auto systemd_spec = StreamSource::parseSystemdJournalSpec(url); systemd_spec.has_value()) {
    const auto start_mode = systemd_spec->start_mode;
    if (systemd_spec->process_name.isEmpty()) {
      if (start_mode == QStringLiteral("now")) {
        return QObject::tr("Systemd journal (from now)");
      }
      if (start_mode == QStringLiteral("today")) {
        return QObject::tr("Systemd journal (from today)");
      }
      if (start_mode == QStringLiteral("7d")) {
        return QObject::tr("Systemd journal (last 7 days)");
      }
      return QObject::tr("Systemd journal");
    }
    if (start_mode == QStringLiteral("now")) {
      return QObject::tr("Systemd: %1 (from now)").arg(systemd_spec->process_name);
    }
    if (start_mode == QStringLiteral("today")) {
      return QObject::tr("Systemd: %1 (from today)").arg(systemd_spec->process_name);
    }
    if (start_mode == QStringLiteral("7d")) {
      return QObject::tr("Systemd: %1 (last 7 days)").arg(systemd_spec->process_name);
    }
    return QObject::tr("Systemd: %1").arg(systemd_spec->process_name);
  }

  if (url.isLocalFile()) {
    const QFileInfo file_info(url.toLocalFile());
    if (!file_info.fileName().isEmpty()) {
      return file_info.fileName();
    }
  }

  if (!url.fileName().isEmpty()) {
    return url.fileName();
  }

  return url.toString();
}

QString AppEngine::displaySourceForUrl(const QUrl& url) {
  if (const auto pipe_spec = StreamSource::parsePipeSpec(url); pipe_spec.has_value()) {
    QStringList channels;
    if (pipe_spec->capture_stdout) {
      channels.push_back(QObject::tr("stdout"));
    }
    if (pipe_spec->capture_stderr) {
      channels.push_back(QObject::tr("stderr"));
    }

    return QObject::tr("Pipe [%1]: %2")
        .arg(channels.join(QStringLiteral("+")))
        .arg(pipe_spec->command);
  }
  if (const auto docker_spec = StreamSource::parseDockerSpec(url); docker_spec.has_value()) {
    const auto label = docker_spec->container_name.isEmpty()
        ? docker_spec->container_id
        : docker_spec->container_name;
    return docker_spec->no_history
        ? QObject::tr("Docker container %1 (%2) starting now").arg(label, docker_spec->container_id)
        : QObject::tr("Docker container %1 (%2)").arg(label, docker_spec->container_id);
  }
  if (const auto adb_spec = StreamSource::parseAdbLogcatSpec(url); adb_spec.has_value()) {
    const auto label = adb_spec->name.isEmpty() ? adb_spec->serial : adb_spec->name;
    return adb_spec->no_history
        ? QObject::tr("ADB logcat %1 (%2) starting now").arg(label, adb_spec->serial)
        : QObject::tr("ADB logcat %1 (%2)").arg(label, adb_spec->serial);
  }
  if (const auto systemd_spec = StreamSource::parseSystemdJournalSpec(url); systemd_spec.has_value()) {
    const auto start_mode = systemd_spec->start_mode;
    if (systemd_spec->process_name.isEmpty()) {
      if (start_mode == QStringLiteral("now")) {
        return QObject::tr("Systemd journal starting now");
      }
      if (start_mode == QStringLiteral("today")) {
        return QObject::tr("Systemd journal from today");
      }
      if (start_mode == QStringLiteral("7d")) {
        return QObject::tr("Systemd journal from the last 7 days");
      }
      return QObject::tr("Systemd journal");
    }
    if (start_mode == QStringLiteral("now")) {
      return QObject::tr("Systemd journal starting now filtered by %1").arg(systemd_spec->process_name);
    }
    if (start_mode == QStringLiteral("today")) {
      return QObject::tr("Systemd journal from today filtered by %1").arg(systemd_spec->process_name);
    }
    if (start_mode == QStringLiteral("7d")) {
      return QObject::tr("Systemd journal from the last 7 days filtered by %1")
          .arg(systemd_spec->process_name);
    }
    return QObject::tr("Systemd journal filtered by %1").arg(systemd_spec->process_name);
  }

  if (url.isLocalFile()) {
    return QDir::toNativeSeparators(url.toLocalFile());
  }

  return url.toString();
}

QUrl AppEngine::canonicalUrl(const QUrl& url) {
  return normalizeSourceUrl(url);
}

void AppEngine::addRecentLogSource(const QUrl& url) {
  const auto canonical = canonicalUrl(url);
  if (!canonical.isValid() || canonical.isEmpty()) {
    return;
  }

  QStringList values = QSettings{}.value(kRecentLogSourcesKey).toStringList();
  const auto encoded = canonical.toString();
  values.removeAll(encoded);
  values.prepend(encoded);
  while (values.size() > kRecentLogSourceLimit) {
    values.removeLast();
  }

  QSettings settings;
  settings.setValue(kRecentLogSourcesKey, values);
  settings.sync();
  loadRecentLogSources();
}

void AppEngine::loadRecentLogSources() {
  const auto values = QSettings{}.value(kRecentLogSourcesKey).toStringList();
  std::vector<QPair<QUrl, QString>> entries;
  entries.reserve(std::min<size_t>(static_cast<size_t>(values.size()), static_cast<size_t>(kRecentLogSourceLimit)));
  for (const auto& value : values) {
    const auto url = canonicalUrl(QUrl(value));
    if (!url.isValid() || url.isEmpty()) {
      continue;
    }

    entries.push_back(qMakePair(url, displaySourceForUrl(url)));
    if (static_cast<int>(entries.size()) == kRecentLogSourceLimit) {
      break;
    }
  }

  recent_logs_.setEntries(std::move(entries));
  emit recentLogsChanged();
}

void AppEngine::addRecentPipeStream(const QUrl& url) {
  const auto canonical = canonicalUrl(url);
  const auto pipe_spec = StreamSource::parsePipeSpec(canonical);
  if (!pipe_spec.has_value()) {
    return;
  }

  const auto normalized_key = normalizedRecentPipeKey(canonical);
  if (normalized_key.isEmpty()) {
    return;
  }

  QStringList values = QSettings{}.value(kRecentPipeStreamsKey).toStringList();
  const auto encoded = canonical.toString();
  values.erase(std::remove_if(values.begin(), values.end(),
                              [&normalized_key](const QString& value) {
                                return normalizedRecentPipeKey(QUrl(value)) == normalized_key;
                              }),
               values.end());
  values.prepend(encoded);
  while (values.size() > kRecentLogSourceLimit) {
    values.removeLast();
  }

  QSettings settings;
  settings.setValue(kRecentPipeStreamsKey, values);
  settings.sync();
  loadRecentPipeStreams();
}

void AppEngine::removeRecentPipeStream(const QUrl& url) {
  const auto canonical = canonicalUrl(url);
  const auto normalized_key = normalizedRecentPipeKey(canonical);
  if (normalized_key.isEmpty()) {
    return;
  }

  QStringList values = QSettings{}.value(kRecentPipeStreamsKey).toStringList();
  values.erase(std::remove_if(values.begin(), values.end(),
                              [&normalized_key](const QString& value) {
                                return normalizedRecentPipeKey(QUrl(value)) == normalized_key;
                              }),
               values.end());

  QSettings settings;
  settings.setValue(kRecentPipeStreamsKey, values);
  settings.sync();
  loadRecentPipeStreams();
}

void AppEngine::loadRecentPipeStreams() {
  const auto values = QSettings{}.value(kRecentPipeStreamsKey).toStringList();
  std::vector<QPair<QUrl, QString>> entries;
  entries.reserve(std::min<size_t>(static_cast<size_t>(values.size()), static_cast<size_t>(kRecentLogSourceLimit)));
  QSet<QString> seen_keys;
  for (const auto& value : values) {
    const auto url = canonicalUrl(QUrl(value));
    const auto pipe_spec = StreamSource::parsePipeSpec(url);
    if (!pipe_spec.has_value()) {
      continue;
    }

    const auto normalized_key = normalizedRecentPipeKey(url);
    if (normalized_key.isEmpty() || seen_keys.contains(normalized_key)) {
      continue;
    }
    seen_keys.insert(normalized_key);
    entries.push_back(qMakePair(url, displaySourceForUrl(url)));
    if (static_cast<int>(entries.size()) == kRecentLogSourceLimit) {
      break;
    }
  }

  recent_pipe_streams_.setEntries(std::move(entries));
  emit recentPipeStreamsChanged();
}

void AppEngine::saveLogSourceMetadata(const QUrl& url, const QString& scanner_name,
                                      std::optional<bool> wrap_log_lines) const {
  const auto canonical = canonicalUrl(url);
  const auto normalized_scanner_name = scanner_name.trimmed();
  if (!canonical.isValid() || canonical.isEmpty()) {
    return;
  }
  if (normalized_scanner_name.isEmpty() && !wrap_log_lines.has_value()) {
    return;
  }

  const auto url_key = QString::fromLatin1(kLogSourceMetadataUrlKey);
  const auto scanner_name_key = QString::fromLatin1(kLogSourceMetadataScannerNameKey);
  const auto wrap_log_lines_key = QString::fromLatin1(kLogSourceMetadataWrapLogLinesKey);
  QVariantList entries =
      sanitizedLogSourceMetadataEntries(QSettings{}.value(kLogSourceMetadataKey));
  const auto encoded_url = canonical.toString();
  QVariantMap previous_entry;
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&encoded_url, &previous_entry](const QVariant& entry_value) {
                                 const auto entry = entry_value.toMap();
                                 const bool matches =
                                     entry.value(QString::fromLatin1(kLogSourceMetadataUrlKey)).toString() == encoded_url;
                                 if (matches) {
                                   previous_entry = entry;
                                 }
                                 return matches;
                               }),
                entries.end());
  QVariantMap metadata_entry;
  metadata_entry.insert(url_key, encoded_url);
  if (!normalized_scanner_name.isEmpty()) {
    metadata_entry.insert(scanner_name_key, normalized_scanner_name);
  } else if (previous_entry.contains(scanner_name_key)) {
    metadata_entry.insert(scanner_name_key, previous_entry.value(scanner_name_key));
  }
  if (wrap_log_lines.has_value()) {
    metadata_entry.insert(wrap_log_lines_key, *wrap_log_lines);
  } else if (previous_entry.contains(wrap_log_lines_key)) {
    metadata_entry.insert(wrap_log_lines_key, previous_entry.value(wrap_log_lines_key));
  }
  entries.prepend(metadata_entry);
  while (entries.size() > kLogSourceMetadataLimit) {
    entries.removeLast();
  }

  QSettings settings;
  settings.setValue(kLogSourceMetadataKey, entries);
  settings.sync();
}

QString AppEngine::savedLogSourceScannerName(const QUrl& url) const {
  const auto canonical = canonicalUrl(url);
  if (!canonical.isValid() || canonical.isEmpty()) {
    return {};
  }

  const auto url_key = QString::fromLatin1(kLogSourceMetadataUrlKey);
  const auto scanner_name_key = QString::fromLatin1(kLogSourceMetadataScannerNameKey);
  const auto encoded_url = canonical.toString();
  const auto entries = sanitizedLogSourceMetadataEntries(QSettings{}.value(kLogSourceMetadataKey));
  for (const auto& entry_value : entries) {
    const auto entry = entry_value.toMap();
    if (entry.value(url_key).toString() == encoded_url) {
      return entry.value(scanner_name_key).toString().trimmed();
    }
  }

  return {};
}

bool AppEngine::savedLogSourceWrapLogLines(const QUrl& url, bool* found) const {
  const auto canonical = canonicalUrl(url);
  if (found) {
    *found = false;
  }
  if (!canonical.isValid() || canonical.isEmpty()) {
    return false;
  }

  const auto url_key = QString::fromLatin1(kLogSourceMetadataUrlKey);
  const auto wrap_log_lines_key = QString::fromLatin1(kLogSourceMetadataWrapLogLinesKey);
  const auto encoded_url = canonical.toString();
  const auto entries = sanitizedLogSourceMetadataEntries(QSettings{}.value(kLogSourceMetadataKey));
  for (const auto& entry_value : entries) {
    const auto entry = entry_value.toMap();
    if (entry.value(url_key).toString() == encoded_url && entry.contains(wrap_log_lines_key)) {
      if (found) {
        *found = true;
      }
      return entry.value(wrap_log_lines_key).toBool();
    }
  }

  return false;
}

}  // namespace lgx
