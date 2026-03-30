#pragma once

#include <vector>

#include <QAbstractListModel>

namespace lgx {

class DockerContainersModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    ContainerIdRole = Qt::UserRole + 1,
    NameRole,
    ImageRole,
    StatusRole,
    TitleRole
  };
  Q_ENUM(Role)

  explicit DockerContainersModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setEntries(std::vector<QStringList> entries);

 private:
  struct Entry {
    QString container_id;
    QString name;
    QString image;
    QString status;
  };

  std::vector<Entry> entries_;
};

}  // namespace lgx
