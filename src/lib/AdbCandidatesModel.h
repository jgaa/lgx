#pragma once

#include <vector>

#include <QAbstractListModel>

namespace lgx {

class AdbCandidatesModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    PathRole = Qt::UserRole + 1,
    VersionRole,
    SourceRole,
    TitleRole
  };
  Q_ENUM(Role)

  explicit AdbCandidatesModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setEntries(std::vector<QStringList> entries);

 private:
  struct Entry {
    QString path;
    QString version;
    QString source;
  };

  std::vector<Entry> entries_;
};

}  // namespace lgx
