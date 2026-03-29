#include "OpenLogsModel.h"

namespace lgx {

OpenLogsModel::OpenLogsModel(QObject* parent)
    : QAbstractListModel(parent) {}

int OpenLogsModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(entries_.size());
}

QVariant OpenLogsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  const auto& entry = entries_[static_cast<size_t>(index.row())];
  switch (role) {
    case TitleRole:
      return entry.title;
    case SourceUrlRole:
      return entry.source_url;
    default:
      return {};
  }
}

QHash<int, QByteArray> OpenLogsModel::roleNames() const {
  return {
      {TitleRole, "title"},
      {SourceUrlRole, "sourceUrl"},
  };
}

int OpenLogsModel::indexOfUrl(const QUrl& url) const {
  for (int i = 0; i < rowCount(); ++i) {
    if (entries_[static_cast<size_t>(i)].source_url == url) {
      return i;
    }
  }

  return -1;
}

QUrl OpenLogsModel::sourceUrlAt(int index) const {
  if (index < 0 || index >= rowCount()) {
    return {};
  }

  return entries_[static_cast<size_t>(index)].source_url;
}

int OpenLogsModel::addOpenLog(QUrl source_url, QString title) {
  const auto existing_index = indexOfUrl(source_url);
  if (existing_index >= 0) {
    return existing_index;
  }

  const auto insert_at = rowCount();
  beginInsertRows({}, insert_at, insert_at);
  entries_.push_back(Entry{
      .source_url = std::move(source_url),
      .title = std::move(title),
  });
  endInsertRows();
  return insert_at;
}

bool OpenLogsModel::removeOpenLogAt(int index) {
  if (index < 0 || index >= rowCount()) {
    return false;
  }

  beginRemoveRows({}, index, index);
  entries_.erase(entries_.begin() + index);
  endRemoveRows();
  return true;
}

}  // namespace lgx
