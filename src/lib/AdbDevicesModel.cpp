#include "AdbDevicesModel.h"

namespace lgx {

AdbDevicesModel::AdbDevicesModel(QObject* parent)
    : QAbstractListModel(parent) {}

int AdbDevicesModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(entries_.size());
}

QVariant AdbDevicesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  const auto& entry = entries_[static_cast<size_t>(index.row())];
  switch (role) {
    case SerialRole:
      return entry.serial;
    case StateRole:
      return entry.state;
    case ModelRole:
      return entry.model;
    case DeviceRole:
      return entry.device;
    case ProductRole:
      return entry.product;
    case TransportIdRole:
      return entry.transport_id;
    case TitleRole:
      return !entry.model.isEmpty() ? entry.model
           : !entry.device.isEmpty() ? entry.device
           : entry.serial;
    default:
      return {};
  }
}

QHash<int, QByteArray> AdbDevicesModel::roleNames() const {
  return {
      {SerialRole, "serial"},
      {StateRole, "state"},
      {ModelRole, "model"},
      {DeviceRole, "device"},
      {ProductRole, "product"},
      {TransportIdRole, "transportId"},
      {TitleRole, "title"},
  };
}

void AdbDevicesModel::setEntries(std::vector<QStringList> entries) {
  beginResetModel();
  entries_.clear();
  entries_.reserve(entries.size());
  for (auto& fields : entries) {
    Entry entry;
    if (!fields.empty()) {
      entry.serial = std::move(fields[0]);
    }
    if (fields.size() > 1) {
      entry.state = std::move(fields[1]);
    }
    if (fields.size() > 2) {
      entry.model = std::move(fields[2]);
    }
    if (fields.size() > 3) {
      entry.device = std::move(fields[3]);
    }
    if (fields.size() > 4) {
      entry.product = std::move(fields[4]);
    }
    if (fields.size() > 5) {
      entry.transport_id = std::move(fields[5]);
    }
    entries_.push_back(std::move(entry));
  }
  endResetModel();
}

}  // namespace lgx
