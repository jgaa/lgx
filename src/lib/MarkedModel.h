#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QUrl>

#include "LogModel.h"

namespace lgx {

class MarkedModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QObject* sourceModel READ sourceModelObject CONSTANT)
  Q_PROPERTY(QUrl sourceUrl READ sourceUrl CONSTANT)

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

  explicit MarkedModel(LogModel* source_model, QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] QObject* sourceModelObject() const noexcept;
  [[nodiscard]] QUrl sourceUrl() const;

  Q_INVOKABLE QString plainTextAt(int row) const;
  Q_INVOKABLE QString rawTextAt(int row) const;
  Q_INVOKABLE int sourceRowAt(int row) const;
  Q_INVOKABLE int lineNoAt(int row) const;
  Q_INVOKABLE QString processNameAt(int row) const;
  Q_INVOKABLE int logLevelAt(int row) const;
  Q_INVOKABLE bool markedAt(int row) const;
  Q_INVOKABLE int markColorAt(int row) const;
  Q_INVOKABLE bool toggleMarkAt(int row, int preferredColor = static_cast<int>(LineMark_Default));

 private:
  struct SparseWindowCache {
    int logical_first{-1};
    int logical_last{-1};
    std::shared_ptr<const SourceWindow> source_window;
    QVector<int> source_rows;
  };

  [[nodiscard]] int proxyRowForSourceRow(int source_row) const;
  [[nodiscard]] const SourceWindowLine* lineAtRow(int row,
                                                  const SourceWindow** window) const;
  void clearCachedWindow() const;
  void rebuildMarkedRows();
  void onSourceRowsChanged();
  void onSourceDataChanged(int first_row, int last_row, const QList<int>& roles);

  QPointer<LogModel> source_model_;
  QVector<int> marked_rows_;
  mutable SparseWindowCache window_cache_;
};

}  // namespace lgx
