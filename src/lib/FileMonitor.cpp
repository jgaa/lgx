#include "FileMonitor.h"

#include <memory>
#include <utility>

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QString>

namespace lgx {
namespace {

class QtFileWatch final : public IFileWatch {
 public:
  QtFileWatch(QString path, FileEventCallback callback, bool directory_watch)
      : path_(std::move(path)),
        callback_(std::move(callback)),
        directory_watch_(directory_watch),
        watcher_(std::make_unique<QFileSystemWatcher>()) {
    if (!watcher_->addPath(path_)) {
      watcher_.reset();
      return;
    }

    if (directory_watch_) {
      QObject::connect(watcher_.get(), &QFileSystemWatcher::directoryChanged,
                       [this](const QString&) { notify(FileEventHint::DirectoryChanged); });
    } else {
      QObject::connect(watcher_.get(), &QFileSystemWatcher::fileChanged, [this](const QString&) {
        const QFileInfo info(path_);
        notify(info.exists() ? FileEventHint::Modified : FileEventHint::Removed);
      });
    }
  }

  [[nodiscard]] bool isActive() const noexcept { return static_cast<bool>(watcher_); }

 private:
  void notify(FileEventHint hint) const {
    if (callback_) {
      callback_(hint);
    }
  }

  QString path_;
  FileEventCallback callback_;
  bool directory_watch_{false};
  std::unique_ptr<QFileSystemWatcher> watcher_;
};

class QtFileMonitor final : public IFileMonitor {
 public:
  [[nodiscard]] std::unique_ptr<IFileWatch> watchFile(
      const std::string& path, FileEventCallback callback) override {
    auto watch = std::make_unique<QtFileWatch>(QString::fromStdString(path), std::move(callback),
                                               false);
    if (!watch->isActive()) {
      return {};
    }
    return watch;
  }

  [[nodiscard]] std::unique_ptr<IFileWatch> watchDirectory(
      const std::string& path, FileEventCallback callback) override {
    auto watch = std::make_unique<QtFileWatch>(QString::fromStdString(path), std::move(callback),
                                               true);
    if (!watch->isActive()) {
      return {};
    }
    return watch;
  }
};

}  // namespace

std::shared_ptr<IFileMonitor> createFallbackFileMonitor() {
  return std::make_shared<QtFileMonitor>();
}

}  // namespace lgx
