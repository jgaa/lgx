#pragma once

#include <array>

#include <QAbstractListModel>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include "LogModel.h"

namespace lgx {

class FilterModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QObject* sourceModel READ sourceModelObject CONSTANT)
  Q_PROPERTY(QUrl sourceUrl READ sourceUrl CONSTANT)
  Q_PROPERTY(QString pattern READ pattern WRITE setPattern NOTIFY patternChanged)
  Q_PROPERTY(bool raw READ raw WRITE setRaw NOTIFY rawChanged)
  Q_PROPERTY(bool regex READ regex WRITE setRegex NOTIFY regexChanged)
  Q_PROPERTY(bool caseInsensitive READ caseInsensitive WRITE setCaseInsensitive NOTIFY caseInsensitiveChanged)
  Q_PROPERTY(bool autoRefresh READ autoRefresh WRITE setAutoRefresh NOTIFY autoRefreshChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
  Q_PROPERTY(QString regexError READ regexError NOTIFY regexErrorChanged)
  Q_PROPERTY(QString scannerName READ scannerName NOTIFY scannerNameChanged)
  Q_PROPERTY(int selectedPid READ selectedPid WRITE setSelectedPid NOTIFY selectedPidChanged)
  Q_PROPERTY(QString selectedProcessName READ selectedProcessName WRITE setSelectedProcessName NOTIFY selectedProcessNameChanged)

 public:
  enum Role {
    SourceRowRole = Qt::UserRole + 1,
    LineNoRole,
    ProcessNameRole,
    FunctionNameRole,
    LogLevelRole,
    MarkedRole,
    MarkColorRole,
    RawMessageRole,
    MessageRole,
    DateRole,
    TagsRole,
    ThreadIdRole,
    PidRole,
    TidRole
  };
  Q_ENUM(Role)

  explicit FilterModel(LogModel* source_model, QObject* parent = nullptr);
  ~FilterModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] QObject* sourceModelObject() const noexcept;
  [[nodiscard]] QUrl sourceUrl() const;
  [[nodiscard]] QString pattern() const;
  [[nodiscard]] bool regex() const noexcept;
  [[nodiscard]] bool raw() const noexcept;
  [[nodiscard]] bool caseInsensitive() const noexcept;
  [[nodiscard]] bool autoRefresh() const noexcept;
  [[nodiscard]] bool dirty() const noexcept;
  [[nodiscard]] QString regexError() const;
  [[nodiscard]] QString scannerName() const;
  [[nodiscard]] int selectedPid() const noexcept;
  [[nodiscard]] QString selectedProcessName() const;

  Q_INVOKABLE QString plainTextAt(int row) const;
  Q_INVOKABLE QString rawTextAt(int row) const;
  Q_INVOKABLE int sourceRowAt(int row) const;
  Q_INVOKABLE int proxyRowAtOrAfterSourceRow(int source_row) const;
  Q_INVOKABLE int lineNoAt(int row) const;
  Q_INVOKABLE QString processNameAt(int row) const;
  Q_INVOKABLE int logLevelAt(int row) const;
  Q_INVOKABLE int pidAt(int row) const;
  Q_INVOKABLE int tidAt(int row) const;
  Q_INVOKABLE QVariantList systemdProcesses() const;
  Q_INVOKABLE bool markedAt(int row) const;
  Q_INVOKABLE int markColorAt(int row) const;
  Q_INVOKABLE bool toggleMarkAt(int row, int preferredColor = static_cast<int>(LineMark_Default));
  Q_INVOKABLE bool levelEnabled(int level) const noexcept;
  Q_INVOKABLE void setLevelEnabled(int level, bool enabled);
  Q_INVOKABLE void refresh();

  void prepareForRelease();

 public slots:
  void setPattern(const QString& pattern);
  void setRaw(bool enabled);
  void setRegex(bool enabled);
  void setCaseInsensitive(bool enabled);
  void setAutoRefresh(bool enabled);
  void setSelectedPid(int pid);
  void setSelectedProcessName(const QString& name);

 signals:
  void patternChanged();
  void rawChanged();
  void regexChanged();
  void caseInsensitiveChanged();
  void autoRefreshChanged();
  void dirtyChanged();
  void regexErrorChanged();
  void levelsChanged();
  void scannerNameChanged();
  void selectedPidChanged();
  void selectedProcessNameChanged();

 private:
  struct SparseWindowCache {
    int logical_first{-1};
    int logical_last{-1};
    std::shared_ptr<const SourceWindow> source_window;
    QVector<int> source_rows;
    bool raw{false};
  };

  [[nodiscard]] bool matchesSourceRow(int source_row) const;
  [[nodiscard]] int proxyRowForSourceRow(int source_row) const;
  [[nodiscard]] const SourceWindowLine* lineAtRow(int row, bool raw,
                                                  const SourceWindow** window) const;
  void clearCachedWindows() const;
  void scheduleRefresh();
  void markDirty();
  void rebuildFilter();
  void updateRegex();
  void onSourceRowsChanged();
  void onSourceDataChanged(int first_row, int last_row, const QList<int>& roles);

  QPointer<LogModel> source_model_;
  QVector<int> filtered_rows_;
  QString pattern_;
  QRegularExpression compiled_regex_;
  QString regex_error_;
  std::array<bool, number_of_log_levels> enabled_levels_{};
  QTimer refresh_timer_;
  bool raw_{false};
  bool regex_{false};
  bool case_insensitive_{false};
  bool auto_refresh_{true};
  bool dirty_{false};
  int selected_pid_{0};
  QString selected_process_name_;
  mutable SparseWindowCache parsed_window_;
  mutable SparseWindowCache raw_window_;
};

}  // namespace lgx
