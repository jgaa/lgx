#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

#include "LogModel.h"
#include "OpenLogsModel.h"
#include "RecentLogsModel.h"
#include "AdbCandidatesModel.h"
#include "AdbDevicesModel.h"
#include "DockerContainersModel.h"

namespace lgx {
class FilterModel;
class MarkedModel;
}

namespace lgx {

/**
 * @brief Application engine and service registry.
 *
 * AppEngine owns application-wide services exposed to QML. For log views it
 * acts as a factory and registry, returning a single shared LogModel instance
 * for each canonical QUrl and tracking how many QML-side consumers currently
 * claim that model.
 */
class AppEngine : public QObject {
  Q_OBJECT
  Q_PROPERTY(QAbstractItemModel* openLogs READ openLogs CONSTANT)
  Q_PROPERTY(int openLogCount READ openLogCount NOTIFY openLogCountChanged)
  Q_PROPERTY(QAbstractItemModel* recentLogs READ recentLogs CONSTANT)
  Q_PROPERTY(int recentLogCount READ recentLogCount NOTIFY recentLogsChanged)
  Q_PROPERTY(QAbstractItemModel* recentPipeStreams READ recentPipeStreams CONSTANT)
  Q_PROPERTY(int recentPipeStreamCount READ recentPipeStreamCount NOTIFY recentPipeStreamsChanged)
  Q_PROPERTY(bool dockerAvailable READ dockerAvailable CONSTANT)
  Q_PROPERTY(QString adbExecutablePath READ adbExecutablePath WRITE setAdbExecutablePath NOTIFY adbExecutablePathChanged)
  Q_PROPERTY(bool adbAvailable READ adbAvailable NOTIFY adbAvailabilityChanged)
  Q_PROPERTY(QAbstractItemModel* adbCandidates READ adbCandidates CONSTANT)
  Q_PROPERTY(int adbCandidateCount READ adbCandidateCount NOTIFY adbCandidatesChanged)
  Q_PROPERTY(QString adbScanError READ adbScanError NOTIFY adbScanErrorChanged)
  Q_PROPERTY(QAbstractItemModel* adbDevices READ adbDevices CONSTANT)
  Q_PROPERTY(int adbDeviceCount READ adbDeviceCount NOTIFY adbDevicesChanged)
  Q_PROPERTY(QString adbDeviceQueryError READ adbDeviceQueryError NOTIFY adbDeviceQueryErrorChanged)
  Q_PROPERTY(QAbstractItemModel* dockerContainers READ dockerContainers CONSTANT)
  Q_PROPERTY(int dockerContainerCount READ dockerContainerCount NOTIFY dockerContainersChanged)
  Q_PROPERTY(QString dockerContainerQueryError READ dockerContainerQueryError NOTIFY dockerContainerQueryErrorChanged)
  Q_PROPERTY(QStringList logScanners READ logScanners CONSTANT)
  Q_PROPERTY(int currentOpenLogIndex READ currentOpenLogIndex WRITE setCurrentOpenLogIndex NOTIFY currentOpenLogIndexChanged)
  Q_PROPERTY(QUrl currentOpenLogSourceUrl READ currentOpenLogSourceUrl NOTIFY currentOpenLogSourceUrlChanged)
  Q_PROPERTY(QObject* currentLogModel READ currentLogModel NOTIFY currentLogModelChanged)

 public:
  explicit AppEngine(QObject* parent = nullptr);

  /**
   * @brief Access the singleton application engine.
   *
   * @return Shared engine instance.
   */
  static AppEngine& instance();

  [[nodiscard]] QAbstractItemModel* openLogs() noexcept;
  [[nodiscard]] int openLogCount() const noexcept;
  [[nodiscard]] QAbstractItemModel* recentLogs() noexcept;
  [[nodiscard]] int recentLogCount() const noexcept;
  [[nodiscard]] QAbstractItemModel* recentPipeStreams() noexcept;
  [[nodiscard]] int recentPipeStreamCount() const noexcept;
  [[nodiscard]] bool dockerAvailable() const noexcept;
  [[nodiscard]] QString adbExecutablePath() const noexcept;
  [[nodiscard]] bool adbAvailable() const noexcept;
  [[nodiscard]] QAbstractItemModel* adbCandidates() noexcept;
  [[nodiscard]] int adbCandidateCount() const noexcept;
  [[nodiscard]] QString adbScanError() const noexcept;
  [[nodiscard]] QAbstractItemModel* adbDevices() noexcept;
  [[nodiscard]] int adbDeviceCount() const noexcept;
  [[nodiscard]] QString adbDeviceQueryError() const noexcept;
  [[nodiscard]] QAbstractItemModel* dockerContainers() noexcept;
  [[nodiscard]] int dockerContainerCount() const noexcept;
  [[nodiscard]] QString dockerContainerQueryError() const noexcept;
  [[nodiscard]] QStringList logScanners() const;
  [[nodiscard]] int currentOpenLogIndex() const noexcept;
  [[nodiscard]] QUrl currentOpenLogSourceUrl() const;
  [[nodiscard]] QObject* currentLogModel() const noexcept;
  Q_INVOKABLE void setCurrentOpenLogIndex(int index);
  Q_INVOKABLE void setAdbExecutablePath(const QString& path);

  /**
   * @brief Open one log source in the UI tab model.
   *
   * This does not eagerly instantiate a LogModel. The model is created lazily
   * when a QML LogView claims it.
   *
   * @param url Log source URL.
   * @return Open tab index, or -1 for an invalid URL.
   */
  Q_INVOKABLE int openLogSource(const QUrl& url);
  Q_INVOKABLE int openPipeStream(const QString& command, bool include_stdout, bool include_stderr,
                                 bool remember_in_recents = true);
  Q_INVOKABLE int openDockerContainerStream(const QString& container_id,
                                            const QString& container_name = {});
  Q_INVOKABLE int openAdbLogcatStream(const QString& serial, const QString& name = {});
  Q_INVOKABLE int scanAdbExecutables();
  Q_INVOKABLE bool refreshAdbDevices();
  Q_INVOKABLE bool refreshDockerContainers();

  /**
   * @brief Prompt the user to choose a local file and open it as a log source.
   *
   * @param initial_url Optional active source used to seed the initial directory
   * for the file picker when it points to a local file.
   * @return Open tab index, or -1 when cancelled or invalid.
   */
  Q_INVOKABLE int openLogFile(const QUrl& initial_url = {});
  Q_INVOKABLE int openRecentLogSourceAt(int index);
  Q_INVOKABLE int openRecentPipeStreamAt(int index);
  Q_INVOKABLE bool closeOpenLogAt(int index);

  /**
   * @brief Resolve the currently open source URL for one tab index.
   *
   * @param index Open-log row index.
   * @return Source URL, or an invalid URL when out of range.
   */
  Q_INVOKABLE QUrl openLogSourceUrlAt(int index) const;
  Q_INVOKABLE QString displaySourceTextForUrl(const QUrl& url) const;
  Q_INVOKABLE void copyTextToClipboard(const QString& text) const;
  Q_INVOKABLE void logUiTrace(const QString& message) const;

  /**
   * @brief Create or reuse a model for one log source URL.
   *
   * The same LogModel instance is returned for repeated requests targeting the
   * same canonical URL. AppEngine retains ownership.
   *
   * @param url Log source URL.
   * @return Shared LogModel instance as QObject, or nullptr for an invalid URL.
   */
  Q_INVOKABLE QObject* createLogModel(const QUrl& url);
  Q_INVOKABLE QObject* createFilterModel(const QUrl& url);
  Q_INVOKABLE QObject* createMarkedModel(const QUrl& url);

  /**
   * @brief Release one QML-side claim on a previously acquired model.
   *
   * When the last claim is released, the model remains owned by AppEngine but
   * is removed from the registry and scheduled for deletion.
   *
   * @param url Log source URL.
   */
  Q_INVOKABLE void releaseLogModel(const QUrl& url);
  Q_INVOKABLE void releaseFilterModel(QObject* model);
  Q_INVOKABLE void releaseMarkedModel(QObject* model);

  /**
   * @brief Resolve the currently registered model for a URL.
   *
   * @param url Log source URL.
   * @return Registered model, or nullptr when none exists.
   */
  [[nodiscard]] LogModel* modelForUrl(const QUrl& url) const;

  /**
   * @brief Read the current retain count for a registered model.
   *
   * @param url Log source URL.
   * @return Number of active claims tracked for the model.
   */
  [[nodiscard]] int retainCountForUrl(const QUrl& url) const;
  void saveSessionState() const;
  void restoreSavedSession();

 signals:
  void openLogCountChanged();
  void recentLogsChanged();
  void recentPipeStreamsChanged();
  void adbExecutablePathChanged();
  void adbAvailabilityChanged();
  void adbCandidatesChanged();
  void adbScanErrorChanged();
  void adbDevicesChanged();
  void adbDeviceQueryErrorChanged();
  void dockerContainersChanged();
  void dockerContainerQueryErrorChanged();
  void currentOpenLogIndexChanged();
  void currentOpenLogSourceUrlChanged();
  void currentLogModelChanged();

 private:
  [[nodiscard]] static QString titleForUrl(const QUrl& url);
  [[nodiscard]] static QString displaySourceForUrl(const QUrl& url);
  int openLogSourceInternal(const QUrl& url, bool add_to_recent);
  void addRecentLogSource(const QUrl& url);
  void loadRecentLogSources();
  void addRecentPipeStream(const QUrl& url);
  void removeRecentPipeStream(const QUrl& url);
  void loadRecentPipeStreams();
  void updateCurrentLogModel();

  struct ModelEntry {
    QPointer<LogModel> model;
    int retain_count{};
  };

  [[nodiscard]] static QUrl canonicalUrl(const QUrl& url);

  OpenLogsModel open_logs_{this};
  RecentLogsModel recent_logs_{this};
  RecentLogsModel recent_pipe_streams_{this};
  AdbCandidatesModel adb_candidates_{this};
  AdbDevicesModel adb_devices_{this};
  DockerContainersModel docker_containers_{this};
  QHash<QUrl, ModelEntry> models_;
  QString adb_scan_error_;
  QString adb_device_query_error_;
  QString docker_executable_;
  QString docker_container_query_error_;
  int current_open_log_index_{-1};
  QUrl current_open_log_source_url_;
  QPointer<LogModel> current_log_model_;
};

}  // namespace lgx
