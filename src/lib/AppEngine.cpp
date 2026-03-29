#include "AppEngine.h"

#include <memory>

#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QSettings>

#include <algorithm>

#include "FileSource.h"
#include "LogScanner.h"
#include "UiSettings.h"

namespace lgx {
namespace {

constexpr int kRecentLogSourceLimit = 25;

}  // namespace

AppEngine::AppEngine(QObject* parent)
    : QObject(parent) {
  connect(&open_logs_, &QAbstractItemModel::rowsInserted, this, &AppEngine::openLogCountChanged);
  connect(&open_logs_, &QAbstractItemModel::rowsRemoved, this, &AppEngine::openLogCountChanged);
  connect(&open_logs_, &QAbstractItemModel::modelReset, this, &AppEngine::openLogCountChanged);
  loadRecentLogSources();
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

QAbstractItemModel* AppEngine::recentLogs() noexcept {
  return &recent_logs_;
}

int AppEngine::recentLogCount() const noexcept {
  return recent_logs_.rowCount();
}

QStringList AppEngine::logScanners() const {
  return availableLogScannerNames();
}

int AppEngine::openLogSource(const QUrl& url) {
  return openLogSourceInternal(url, true);
}

int AppEngine::openLogSourceInternal(const QUrl& url, bool add_to_recent) {
  const auto canonical = canonicalUrl(url);
  if (!canonical.isValid() || canonical.isEmpty()) {
    return -1;
  }

  if (add_to_recent) {
    addRecentLogSource(canonical);
  }

  return open_logs_.addOpenLog(canonical, titleForUrl(canonical));
}

int AppEngine::openLogFile() {
  const auto selected = QFileDialog::getOpenFileUrl(
      nullptr,
      tr("Open Log File"),
      {},
      tr("Log files (*.log *.txt);;All files (*)"));
  if (!selected.isValid() || selected.isEmpty()) {
    return -1;
  }

  return openLogSource(selected);
}

int AppEngine::openRecentLogSourceAt(int index) {
  return openLogSource(recent_logs_.sourceUrlAt(index));
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

QObject* AppEngine::createLogModel(const QUrl& url) {
  const auto canonical = canonicalUrl(url);
  if (!canonical.isValid() || canonical.isEmpty()) {
    return nullptr;
  }

  auto it = models_.find(canonical);
  if (it != models_.end() && it->model) {
    ++it->retain_count;
    return it->model;
  }

  auto* model = new LogModel(canonical, this);
  QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);

  if (canonical.isLocalFile()) {
    auto source = std::make_unique<FileSource>();
    source->open(canonical.toLocalFile().toStdString());
    model->setSource(std::move(source));
    model->setFollowing(UiSettings::instance().followLiveLogsByDefault());
    model->loadFromSource();
  } else {
    model->markFailed();
  }

  models_.insert(canonical, ModelEntry{.model = model, .retain_count = 1});
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

void AppEngine::saveSessionState() const {
  QStringList open_log_sources;
  open_log_sources.reserve(open_logs_.rowCount());
  for (int index = 0; index < open_logs_.rowCount(); ++index) {
    const auto url = open_logs_.sourceUrlAt(index);
    if (url.isValid() && !url.isEmpty()) {
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
  if (url.isLocalFile()) {
    return QDir::toNativeSeparators(url.toLocalFile());
  }

  return url.toString();
}

QUrl AppEngine::canonicalUrl(const QUrl& url) {
  return url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
}

void AppEngine::addRecentLogSource(const QUrl& url) {
  const auto canonical = canonicalUrl(url);
  if (!canonical.isValid() || canonical.isEmpty()) {
    return;
  }

  QStringList values = QSettings{}.value("session/recentLogSources").toStringList();
  const auto encoded = canonical.toString();
  values.removeAll(encoded);
  values.prepend(encoded);
  while (values.size() > kRecentLogSourceLimit) {
    values.removeLast();
  }

  QSettings settings;
  settings.setValue("session/recentLogSources", values);
  settings.sync();
  loadRecentLogSources();
}

void AppEngine::loadRecentLogSources() {
  const auto values = QSettings{}.value("session/recentLogSources").toStringList();
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

}  // namespace lgx
