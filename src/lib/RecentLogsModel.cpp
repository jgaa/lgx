#include "RecentLogsModel.h"

namespace lgx {

RecentLogsModel::RecentLogsModel(QObject* parent)
    : QAbstractListModel(parent) {}

int RecentLogsModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(entries_.size());
}

QVariant RecentLogsModel::data(const QModelIndex& index, int role) const {
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

QHash<int, QByteArray> RecentLogsModel::roleNames() const {
  return {
      {TitleRole, "title"},
      {SourceUrlRole, "sourceUrl"},
  };
}

void RecentLogsModel::setEntries(std::vector<QPair<QUrl, QString>> entries) {
  beginResetModel();
  entries_.clear();
  entries_.reserve(entries.size());
  for (auto& entry : entries) {
    entries_.push_back(Entry{
        .source_url = std::move(entry.first),
        .title = std::move(entry.second),
    });
  }
  endResetModel();
}

QUrl RecentLogsModel::sourceUrlAt(int index) const {
  if (index < 0 || index >= rowCount()) {
    return {};
  }

  return entries_[static_cast<size_t>(index)].source_url;
}

}  // namespace lgx
