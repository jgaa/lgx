#pragma once

#include <functional>
#include <memory>
#include <string>

namespace lgx {

enum class FileEventHint {
  Modified,
  Removed,
  Renamed,
  DirectoryChanged,
  Unknown
};

using FileEventCallback = std::function<void(FileEventHint)>;

class IFileWatch {
 public:
  virtual ~IFileWatch() = default;
};

class IFileMonitor {
 public:
  virtual ~IFileMonitor() = default;

  [[nodiscard]] virtual std::unique_ptr<IFileWatch> watchFile(
      const std::string& path, FileEventCallback callback) = 0;

  [[nodiscard]] virtual std::unique_ptr<IFileWatch> watchDirectory(
      const std::string& path, FileEventCallback callback) = 0;
};

[[nodiscard]] std::shared_ptr<IFileMonitor> createFallbackFileMonitor();

}  // namespace lgx
