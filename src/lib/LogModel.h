#pragma once

#include <memory>
#include <vector>

#include <QAbstractListModel>
#include <QTimer>
#include <QUrl>

#include "LogSource.h"
#include "LogTypes.h"

namespace lgx {

/**
 * @brief QML-facing list model for one open log source.
 *
 * AppEngine owns LogModel instances and reuses a single model per canonical log
 * source URL. The model currently provides row storage and state/role plumbing;
 * actual source loading and population can be added later without changing the
 * QML-facing contract.
 */
class LogModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(State state READ state NOTIFY stateChanged)
  Q_PROPERTY(QUrl sourceUrl READ sourceUrl CONSTANT)
  Q_PROPERTY(bool following READ following WRITE setFollowing NOTIFY followingChanged)
  Q_PROPERTY(bool active READ active NOTIFY activeChanged)
  Q_PROPERTY(QString scannerName READ scannerName NOTIFY scannerNameChanged)
  Q_PROPERTY(QString requestedScannerName READ requestedScannerName WRITE setRequestedScannerName NOTIFY scannerNameChanged)
  Q_PROPERTY(double linesPerSecond READ linesPerSecond NOTIFY linesPerSecondChanged)
  Q_PROPERTY(qulonglong fileSize READ fileSize NOTIFY fileSizeChanged)

 public:
  /**
   * @brief Lifecycle state exposed to QML.
   */
  enum State {
    INITIALIZING = 0,
    READY = 1,
    FAILED = 2
  };
  Q_ENUM(State)

  /**
   * @brief Row roles exposed to QML delegates.
   */
  enum Role {
    LineNoRole = Qt::UserRole + 1,
    FunctionNameRole,
    LogLevelRole,
    RawMessageRole,
    MessageRole,
    DateRole,
    TagsRole,
    ThreadIdRole
  };
  Q_ENUM(Role)

  explicit LogModel(QUrl source_url, QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  Q_INVOKABLE QString plainTextAt(int row) const;
  Q_INVOKABLE int logLevelAt(int row) const;
  Q_INVOKABLE int nextLineOfLevel(int row, int logLevel) const;
  Q_INVOKABLE int previousLineOfLevel(int row, int logLevel) const;
  Q_INVOKABLE void setFollowing(bool enabled);
  Q_INVOKABLE void toggleFollowing();
  Q_INVOKABLE void setRequestedScannerName(const QString& name);

  [[nodiscard]] State state() const noexcept;
  [[nodiscard]] const QUrl& sourceUrl() const noexcept;
  [[nodiscard]] LogSource* source() const noexcept;
  [[nodiscard]] bool following() const noexcept;
  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] QString scannerName() const;
  [[nodiscard]] QString requestedScannerName() const;
  [[nodiscard]] double linesPerSecond() const noexcept;
  [[nodiscard]] qulonglong fileSize() const noexcept;

  /**
   * @brief Replace all model rows.
   *
   * This is intended for future source integration code.
   *
   * @param rows New row set.
   */
  void setRows(std::vector<LogRow> rows);

  /**
   * @brief Append one row to the model.
   *
   * @param row Row to append.
   */
  void appendRow(LogRow row);

  /**
   * @brief Transition the model into the ready state.
   */
  void markReady();

  /**
   * @brief Transition the model into the failed state.
   */
  void markFailed();

  /**
   * @brief Attach a concrete log source implementation to the model.
   *
   * The model owns the source and uses its callbacks to stay in sync.
   *
   * @param source Source instance.
   */
  void setSource(std::unique_ptr<LogSource> source);

  /**
   * @brief Start indexing and populate rows from the attached source.
   */
  void loadFromSource();

signals:
  void stateChanged();
  void followingChanged();
  void activeChanged();
  void scannerNameChanged();
  void linesPerSecondChanged();
  void fileSizeChanged();

 private:
  void markActiveForRecentLines();
  void setActive(bool active);
  void refreshSourceMetrics();
  void replaceRowsFromSource();
  void appendRowsFromSource(uint64_t first_line, uint64_t count);
  void setState(State state);

  QUrl source_url_;
  State state_{INITIALIZING};
  std::vector<LogRow> rows_;
  std::unique_ptr<LogSource> source_;
  bool reset_pending_{false};
  bool following_{false};
  bool active_{false};
  double lines_per_second_{0.0};
  qulonglong file_size_{0};
  QTimer active_timer_;
  QTimer source_metrics_timer_;
};

}  // namespace lgx
