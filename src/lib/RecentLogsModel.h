#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QUrl>

namespace lgx {

class RecentLogsModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    TitleRole = Qt::UserRole + 1,
    SourceUrlRole
  };
  Q_ENUM(Role)

  explicit RecentLogsModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setEntries(std::vector<QPair<QUrl, QString>> entries);
  [[nodiscard]] QUrl sourceUrlAt(int index) const;

 private:
  struct Entry {
    QUrl source_url;
    QString title;
  };

  std::vector<Entry> entries_;
};

}  // namespace lgx
