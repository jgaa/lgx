#pragma once

#include <vector>

#include <QAbstractListModel>

namespace lgx {

class AdbDevicesModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    SerialRole = Qt::UserRole + 1,
    StateRole,
    ModelRole,
    DeviceRole,
    ProductRole,
    TransportIdRole,
    TitleRole
  };
  Q_ENUM(Role)

  explicit AdbDevicesModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setEntries(std::vector<QStringList> entries);

 private:
  struct Entry {
    QString serial;
    QString state;
    QString model;
    QString device;
    QString product;
    QString transport_id;
  };

  std::vector<Entry> entries_;
};

}  // namespace lgx
