#include "AdbCandidatesModel.h"

namespace lgx {

AdbCandidatesModel::AdbCandidatesModel(QObject* parent)
    : QAbstractListModel(parent) {}

int AdbCandidatesModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(entries_.size());
}

QVariant AdbCandidatesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  const auto& entry = entries_[static_cast<size_t>(index.row())];
  switch (role) {
    case PathRole:
      return entry.path;
    case VersionRole:
      return entry.version;
    case SourceRole:
      return entry.source;
    case TitleRole:
      return entry.version.isEmpty() ? entry.path : QStringLiteral("%1  (%2)").arg(entry.path, entry.version);
    default:
      return {};
  }
}

QHash<int, QByteArray> AdbCandidatesModel::roleNames() const {
  return {
      {PathRole, "path"},
      {VersionRole, "version"},
      {SourceRole, "source"},
      {TitleRole, "title"},
  };
}

void AdbCandidatesModel::setEntries(std::vector<QStringList> entries) {
  beginResetModel();
  entries_.clear();
  entries_.reserve(entries.size());
  for (auto& fields : entries) {
    Entry entry;
    if (!fields.empty()) {
      entry.path = std::move(fields[0]);
    }
    if (fields.size() > 1) {
      entry.version = std::move(fields[1]);
    }
    if (fields.size() > 2) {
      entry.source = std::move(fields[2]);
    }
    entries_.push_back(std::move(entry));
  }
  endResetModel();
}

}  // namespace lgx
