#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QUrl>

namespace lgx {

class OpenLogsModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    TitleRole = Qt::UserRole + 1,
    SourceUrlRole
  };
  Q_ENUM(Role)

  explicit OpenLogsModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] int indexOfUrl(const QUrl& url) const;
  [[nodiscard]] QUrl sourceUrlAt(int index) const;
  int addOpenLog(QUrl source_url, QString title);
  bool removeOpenLogAt(int index);

 private:
  struct Entry {
    QUrl source_url;
    QString title;
  };

  std::vector<Entry> entries_;
};

}  // namespace lgx
