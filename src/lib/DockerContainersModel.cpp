#include "DockerContainersModel.h"

namespace lgx {

DockerContainersModel::DockerContainersModel(QObject* parent)
    : QAbstractListModel(parent) {}

int DockerContainersModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(entries_.size());
}

QVariant DockerContainersModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  const auto& entry = entries_[static_cast<size_t>(index.row())];
  switch (role) {
    case ContainerIdRole:
      return entry.container_id;
    case NameRole:
      return entry.name;
    case ImageRole:
      return entry.image;
    case StatusRole:
      return entry.status;
    case TitleRole:
      return entry.name.isEmpty() ? entry.container_id : entry.name;
    default:
      return {};
  }
}

QHash<int, QByteArray> DockerContainersModel::roleNames() const {
  return {
      {ContainerIdRole, "containerId"},
      {NameRole, "name"},
      {ImageRole, "image"},
      {StatusRole, "status"},
      {TitleRole, "title"},
  };
}

void DockerContainersModel::setEntries(std::vector<QStringList> entries) {
  beginResetModel();
  entries_.clear();
  entries_.reserve(entries.size());
  for (auto& fields : entries) {
    Entry entry;
    if (!fields.empty()) {
      entry.container_id = std::move(fields[0]);
    }
    if (fields.size() > 1) {
      entry.name = std::move(fields[1]);
    }
    if (fields.size() > 2) {
      entry.image = std::move(fields[2]);
    }
    if (fields.size() > 3) {
      entry.status = std::move(fields[3]);
    }
    entries_.push_back(std::move(entry));
  }
  endResetModel();
}

}  // namespace lgx
