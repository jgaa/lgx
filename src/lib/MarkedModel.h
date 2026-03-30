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
    FunctionNameRole,
    LogLevelRole,
    MarkedRole,
    MarkColorRole,
    RawMessageRole,
    MessageRole,
    DateRole,
    TagsRole,
    ThreadIdRole
  };
  Q_ENUM(Role)

  explicit MarkedModel(LogModel* source_model, QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] QObject* sourceModelObject() const noexcept;
  [[nodiscard]] QUrl sourceUrl() const;

  Q_INVOKABLE QString plainTextAt(int row) const;
  Q_INVOKABLE int sourceRowAt(int row) const;
  Q_INVOKABLE int lineNoAt(int row) const;
  Q_INVOKABLE int logLevelAt(int row) const;
  Q_INVOKABLE bool markedAt(int row) const;
  Q_INVOKABLE int markColorAt(int row) const;
  Q_INVOKABLE bool toggleMarkAt(int row, int preferredColor = static_cast<int>(LineMark_Default));

 private:
  [[nodiscard]] int proxyRowForSourceRow(int source_row) const;
  void rebuildMarkedRows();
  void onSourceRowsChanged();
  void onSourceDataChanged(int first_row, int last_row, const QList<int>& roles);

  QPointer<LogModel> source_model_;
  QVector<int> marked_rows_;
};

}  // namespace lgx
