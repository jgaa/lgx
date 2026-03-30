#pragma once

#include <memory>
#include <optional>
#include <string>

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "FileSource.h"
#include "LogSource.h"

namespace lgx {

struct PipeStreamSpec {
  QString instance_id;
  QString command;
  bool capture_stdout{true};
  bool capture_stderr{true};
};

struct DockerStreamSpec {
  QString instance_id;
  QString container_id;
  QString container_name;
};

struct AdbLogcatSpec {
  QString instance_id;
  QString serial;
  QString name;
};

class IStreamProvider {
 public:
  struct Callbacks {
    std::function<void(QByteArray)> on_bytes;
    std::function<void(QString)> on_error;
    std::function<void()> on_finished;
  };

  virtual ~IStreamProvider() = default;
  virtual void setCallbacks(Callbacks callbacks) = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
};

/**
 * @brief Stream-backed log source spooled into a temporary file.
 *
 * StreamSource owns a temporary spool file and delegates indexing/page loading
 * to FileSource. Concrete stream backends only provide bytes through the
 * IStreamProvider interface.
 */
class StreamSource final : public LogSource {
 public:
  StreamSource();
  ~StreamSource() override;

  [[nodiscard]] static bool isSupportedUrl(const QUrl& url);
  [[nodiscard]] static QUrl makePipeUrl(const QString& command, bool include_stdout,
                                        bool include_stderr);
  [[nodiscard]] static std::optional<PipeStreamSpec> parsePipeSpec(const QUrl& url);
  [[nodiscard]] static QUrl makeDockerUrl(const QString& container_id,
                                          const QString& container_name = {});
  [[nodiscard]] static std::optional<DockerStreamSpec> parseDockerSpec(const QUrl& url);
  [[nodiscard]] static QUrl makeAdbLogcatUrl(const QString& serial,
                                             const QString& name = {});
  [[nodiscard]] static std::optional<AdbLogcatSpec> parseAdbLogcatSpec(const QUrl& url);

  void setCallbacks(SourceCallbacks callbacks) override;
  [[nodiscard]] std::string path() const override;
  void open(const std::string& path) override;
  void close() override;
  void startIndexing() override;
  void refresh() override;
  void setFollowing(bool enabled) override;
  [[nodiscard]] std::string scannerName() const override;
  [[nodiscard]] std::string requestedScannerName() const override;
  void setRequestedScannerName(std::string name) override;
  [[nodiscard]] SourceSnapshot snapshot() const override;
  [[nodiscard]] uint64_t fileSize() const override;
  [[nodiscard]] double linesPerSecond() const override;
  void fetchLines(uint64_t first_line, size_t count,
                  std::function<void(SourceLines)> on_ready) override;
  [[nodiscard]] std::optional<uint64_t> nextLineWithLevel(uint64_t after_line,
                                                          LogLevel level) const override;
  [[nodiscard]] std::optional<uint64_t> previousLineWithLevel(uint64_t before_line,
                                                              LogLevel level) const override;

 private:
  [[nodiscard]] static std::unique_ptr<IStreamProvider> createProvider(const QUrl& url);
  [[nodiscard]] bool ensureSpoolFile();
  void enqueueProviderBytes(QByteArray bytes);
  void flushPendingBytes();
  void fail(QString message);

  FileSource spool_source_{std::shared_ptr<IFileMonitor>{}};
  QTemporaryDir spool_dir_;
  QFile spool_file_;
  QTimer flush_timer_;
  QByteArray pending_bytes_;
  QString spool_path_;
  std::unique_ptr<IStreamProvider> provider_;
  bool following_{false};
  bool failed_{false};
};

}  // namespace lgx
