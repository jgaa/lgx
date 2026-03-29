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
  Q_PROPERTY(QStringList logScanners READ logScanners CONSTANT)

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
  [[nodiscard]] QStringList logScanners() const;

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

  /**
   * @brief Prompt the user to choose a local file and open it as a log source.
   *
   * @return Open tab index, or -1 when cancelled or invalid.
   */
  Q_INVOKABLE int openLogFile();
  Q_INVOKABLE int openRecentLogSourceAt(int index);
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

  /**
   * @brief Release one QML-side claim on a previously acquired model.
   *
   * When the last claim is released, the model remains owned by AppEngine but
   * is removed from the registry and scheduled for deletion.
   *
   * @param url Log source URL.
   */
  Q_INVOKABLE void releaseLogModel(const QUrl& url);

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

 private:
  [[nodiscard]] static QString titleForUrl(const QUrl& url);
  [[nodiscard]] static QString displaySourceForUrl(const QUrl& url);
  int openLogSourceInternal(const QUrl& url, bool add_to_recent);
  void addRecentLogSource(const QUrl& url);
  void loadRecentLogSources();

  struct ModelEntry {
    QPointer<LogModel> model;
    int retain_count{};
  };

  [[nodiscard]] static QUrl canonicalUrl(const QUrl& url);

  OpenLogsModel open_logs_{this};
  RecentLogsModel recent_logs_{this};
  QHash<QUrl, ModelEntry> models_;
};

}  // namespace lgx
