#include "MarkedModel.h"

#include <algorithm>

namespace lgx {

MarkedModel::MarkedModel(LogModel* source_model, QObject* parent)
    : QAbstractListModel(parent),
      source_model_(source_model) {
  if (source_model_) {
    connect(source_model_, &QAbstractItemModel::modelReset, this, &MarkedModel::onSourceRowsChanged);
    connect(source_model_, &QAbstractItemModel::rowsInserted, this, [this]() {
      onSourceRowsChanged();
    });
    connect(source_model_, &QAbstractItemModel::rowsRemoved, this, [this]() {
      onSourceRowsChanged();
    });
    connect(source_model_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& top_left, const QModelIndex& bottom_right, const QList<int>& roles) {
              onSourceDataChanged(top_left.row(), bottom_right.row(), roles);
            });
    connect(source_model_, &QObject::destroyed, this, [this]() {
      beginResetModel();
      source_model_ = nullptr;
      marked_rows_.clear();
      endResetModel();
    });
  }

  rebuildMarkedRows();
}

int MarkedModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return marked_rows_.size();
}

QVariant MarkedModel::data(const QModelIndex& index, int role) const {
  if (!source_model_ || !index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  const int source_row = marked_rows_.at(index.row());
  return source_model_->data(source_model_->index(source_row, 0), role);
}

QHash<int, QByteArray> MarkedModel::roleNames() const {
  return {
      {SourceRowRole, "sourceRow"},
      {LineNoRole, "lineNo"},
      {FunctionNameRole, "functionName"},
      {LogLevelRole, "logLevel"},
      {MarkedRole, "marked"},
      {MarkColorRole, "markColor"},
      {RawMessageRole, "rawMessage"},
      {MessageRole, "message"},
      {DateRole, "date"},
      {TagsRole, "tags"},
      {ThreadIdRole, "threadId"},
      {PidRole, "pid"},
      {TidRole, "tid"},
  };
}

QObject* MarkedModel::sourceModelObject() const noexcept {
  return source_model_;
}

QUrl MarkedModel::sourceUrl() const {
  return source_model_ ? source_model_->sourceUrl() : QUrl{};
}

QString MarkedModel::plainTextAt(int row) const {
  if (!source_model_) {
    return {};
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->plainTextAt(source_row) : QString{};
}

int MarkedModel::sourceRowAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return -1;
  }

  return marked_rows_.at(row);
}

int MarkedModel::lineNoAt(int row) const {
  if (!source_model_) {
    return 0;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->lineNoAt(source_row) : 0;
}

int MarkedModel::logLevelAt(int row) const {
  if (!source_model_) {
    return static_cast<int>(LogLevel_Info);
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->logLevelAt(source_row) : static_cast<int>(LogLevel_Info);
}

bool MarkedModel::markedAt(int row) const {
  if (!source_model_) {
    return false;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 && source_model_->markedAt(source_row);
}

int MarkedModel::markColorAt(int row) const {
  if (!source_model_) {
    return static_cast<int>(LineMark_None);
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->markColorAt(source_row) : static_cast<int>(LineMark_None);
}

bool MarkedModel::toggleMarkAt(int row, int preferredColor) {
  if (!source_model_) {
    return false;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 && source_model_->toggleMarkAt(source_row, preferredColor);
}

int MarkedModel::proxyRowForSourceRow(int source_row) const {
  const auto it = std::lower_bound(marked_rows_.cbegin(), marked_rows_.cend(), source_row);
  if (it == marked_rows_.cend() || *it != source_row) {
    return -1;
  }

  return static_cast<int>(std::distance(marked_rows_.cbegin(), it));
}

void MarkedModel::rebuildMarkedRows() {
  QVector<int> next_rows;
  if (source_model_) {
    next_rows.reserve(source_model_->rowCount());
    for (int source_row = 0; source_row < source_model_->rowCount(); ++source_row) {
      if (source_model_->markedAt(source_row)) {
        next_rows.push_back(source_row);
      }
    }
  }

  beginResetModel();
  marked_rows_ = std::move(next_rows);
  endResetModel();
}

void MarkedModel::onSourceRowsChanged() {
  rebuildMarkedRows();
}

void MarkedModel::onSourceDataChanged(int first_row, int last_row, const QList<int>& roles) {
  bool affects_marks = roles.isEmpty();
  bool affects_other_data = roles.isEmpty();
  for (const int role : roles) {
    if (role == LogModel::MarkedRole || role == LogModel::MarkColorRole) {
      affects_marks = true;
    } else {
      affects_other_data = true;
    }
  }

  if (affects_marks) {
    rebuildMarkedRows();
  }

  if (!affects_other_data || marked_rows_.isEmpty()) {
    return;
  }

  for (int source_row = first_row; source_row <= last_row; ++source_row) {
    const int proxy_row = proxyRowForSourceRow(source_row);
    if (proxy_row < 0) {
      continue;
    }

    const QModelIndex proxy_index = index(proxy_row, 0);
    emit dataChanged(proxy_index, proxy_index, roles);
  }
}

}  // namespace lgx
