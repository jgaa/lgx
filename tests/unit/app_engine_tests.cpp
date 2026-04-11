#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "AppEngine.h"
#include "FileSource.h"
#include "StreamSource.h"
#include "UiSettings.h"

namespace lgx {
namespace {

class ScopedTestSettings {
 public:
  ScopedTestSettings()
      : temp_dir_() {
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp_dir_.path());
    QCoreApplication::setOrganizationName(QStringLiteral("lgx-tests"));
    QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSettings settings;
    settings.clear();
    settings.sync();
  }

 private:
  QTemporaryDir temp_dir_;
};

TEST(AppEngineTests, ReusesSameModelForSameUrlAndTracksRetains) {
  auto& engine = AppEngine::instance();
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("example.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_EQ(file.write("line one\nline two\n"), 18);
  file.close();
  const QUrl url = QUrl::fromLocalFile(path);

  engine.releaseLogModel(url);

  auto* first = qobject_cast<LogModel*>(engine.createLogModel(url));
  auto* second = qobject_cast<LogModel*>(engine.createLogModel(url));

  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(first, second);
  EXPECT_EQ(first->state(), LogModel::READY);
  EXPECT_EQ(engine.retainCountForUrl(url), 2);

  engine.releaseLogModel(url);
  EXPECT_EQ(engine.modelForUrl(url), first);
  EXPECT_EQ(engine.retainCountForUrl(url), 1);

  engine.releaseLogModel(url);
  EXPECT_EQ(engine.modelForUrl(url), nullptr);
  EXPECT_EQ(engine.retainCountForUrl(url), 0);
}

TEST(AppEngineTests, LogModelExposesRequestedRoles) {
  LogModel model(QUrl(QStringLiteral("file:///tmp/roles.log")));

  LogRow row;
  row.line_no = 42;
  row.pid = 1234;
  row.tid = 5678;
  row.function_name = QStringLiteral("worker");
  row.log_level = LogLevel_Debug;
  row.raw_message = QStringLiteral("[debug] raw");
  row.message = QStringLiteral("formatted");
  row.date = QDateTime::fromString(QStringLiteral("2026-03-28T10:15:00Z"), Qt::ISODate);
  row.tags = {QStringLiteral("a"), QStringLiteral("b")};
  row.thread_id = QStringLiteral("thread-1");

  model.setRows({row});
  model.markReady();

  const auto index = model.index(0, 0);
  ASSERT_TRUE(index.isValid());
  EXPECT_EQ(model.data(index, LogModel::LineNoRole).toLongLong(), 42);
  EXPECT_EQ(model.data(index, LogModel::FunctionNameRole).toString(), QStringLiteral("worker"));
  EXPECT_EQ(model.data(index, LogModel::LogLevelRole).toInt(), static_cast<int>(LogLevel_Debug));
  EXPECT_EQ(model.data(index, LogModel::PidRole).toInt(), 1234);
  EXPECT_EQ(model.data(index, LogModel::TidRole).toInt(), 5678);
  EXPECT_EQ(model.data(index, LogModel::RawMessageRole).toString(), QStringLiteral("[debug] raw"));
  EXPECT_EQ(model.data(index, LogModel::MessageRole).toString(), QStringLiteral("formatted"));
  EXPECT_EQ(model.data(index, LogModel::DateRole).toDateTime(), row.date);
  EXPECT_EQ(model.data(index, LogModel::TagsRole).toStringList(), row.tags);
  EXPECT_EQ(model.data(index, LogModel::ThreadIdRole).toString(), QStringLiteral("thread-1"));

  const auto roles = model.roleNames();
  EXPECT_EQ(roles.value(LogModel::LineNoRole), QByteArray("lineNo"));
  EXPECT_EQ(roles.value(LogModel::FunctionNameRole), QByteArray("functionName"));
  EXPECT_EQ(roles.value(LogModel::LogLevelRole), QByteArray("logLevel"));
  EXPECT_EQ(roles.value(LogModel::PidRole), QByteArray("pid"));
  EXPECT_EQ(roles.value(LogModel::TidRole), QByteArray("tid"));
  EXPECT_EQ(roles.value(LogModel::RawMessageRole), QByteArray("rawMessage"));
  EXPECT_EQ(roles.value(LogModel::MessageRole), QByteArray("message"));
  EXPECT_EQ(roles.value(LogModel::DateRole), QByteArray("date"));
  EXPECT_EQ(roles.value(LogModel::TagsRole), QByteArray("tags"));
  EXPECT_EQ(roles.value(LogModel::ThreadIdRole), QByteArray("threadId"));
}

TEST(AppEngineTests, LogModelResetsOnSourceReset) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("reset.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_EQ(file.write("line one\nline two\n"), 18);
  file.close();

  LogModel model(QUrl::fromLocalFile(path));
  model.setCurrent(true);
  auto source = std::make_unique<FileSource>();
  source->open(path.toStdString());
  model.setSource(std::move(source));
  model.loadFromSource();

  int model_reset_count = 0;
  int rows_inserted_count = 0;
  QObject::connect(&model, &QAbstractItemModel::modelReset,
                   [&model_reset_count]() { ++model_reset_count; });
  QObject::connect(&model, &QAbstractItemModel::rowsInserted,
                   [&rows_inserted_count]() { ++rows_inserted_count; });

  auto* attached_source = model.source();
  ASSERT_NE(attached_source, nullptr);

  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_EQ(file.write("short\n"), 6);
  file.close();

  attached_source->refresh();

  EXPECT_EQ(model_reset_count, 1);
  EXPECT_GE(rows_inserted_count, 1);
  EXPECT_EQ(model.rowCount(), 1);
  const auto index = model.index(0, 0);
  ASSERT_TRUE(index.isValid());
  EXPECT_EQ(model.data(index, LogModel::RawMessageRole).toString(), QStringLiteral("short"));
}

TEST(AppEngineTests, PersistsRecentLogSourcesAndSessionRestore) {
  ScopedTestSettings scoped_settings;
  QSettings{}.setValue("startup/restoreOpenLogsOnStartup", true);
  AppEngine engine;

  EXPECT_EQ(engine.openLogSource(QUrl(QStringLiteral("file:///tmp/one.log"))), 0);
  EXPECT_EQ(engine.openLogSource(QUrl(QStringLiteral("file:///tmp/two.log"))), 1);
  EXPECT_EQ(engine.openLogSource(QUrl(QStringLiteral("file:///tmp/one.log"))), 0);
  EXPECT_EQ(engine.recentLogCount(), 2);

  engine.saveSessionState();

  AppEngine restored_engine;
  EXPECT_EQ(restored_engine.recentLogCount(), 2);
  EXPECT_EQ(restored_engine.openLogCount(), 0);

  restored_engine.restoreSavedSession();

  EXPECT_EQ(restored_engine.openLogCount(), 2);
  EXPECT_EQ(restored_engine.openLogSourceUrlAt(0), QUrl(QStringLiteral("file:///tmp/one.log")));
  EXPECT_EQ(restored_engine.openLogSourceUrlAt(1), QUrl(QStringLiteral("file:///tmp/two.log")));
  EXPECT_EQ(restored_engine.openRecentLogSourceAt(1), 1);
}

TEST(AppEngineTests, LimitsRecentLogSourcesToTwentyFiveEntries) {
  ScopedTestSettings scoped_settings;
  AppEngine engine;

  for (int index = 0; index < 30; ++index) {
    EXPECT_GE(engine.openLogSource(QUrl(QStringLiteral("file:///tmp/log-%1.log").arg(index))), 0);
  }

  EXPECT_EQ(engine.recentLogCount(), 25);

  AppEngine restored_engine;
  EXPECT_EQ(restored_engine.recentLogCount(), 25);
}

TEST(AppEngineTests, PersistsSourceFormatMetadataAndReappliesIt) {
  ScopedTestSettings scoped_settings;
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("formatted.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_GT(file.write("2026-03-31 10:15:00.123 host INFO 42 {worker} hello\n"), 0);
  file.close();

  const auto url = QUrl::fromLocalFile(path);

  {
    AppEngine engine;
    EXPECT_EQ(engine.openLogSource(url), 0);

    auto* model = qobject_cast<LogModel*>(engine.createLogModel(url));
    ASSERT_NE(model, nullptr);
    model->setRequestedScannerName(QStringLiteral("Logfault"));
    EXPECT_EQ(model->requestedScannerName(), QStringLiteral("Logfault"));
    engine.releaseLogModel(url);
  }

  AppEngine restored_engine;
  auto* restored_model = qobject_cast<LogModel*>(restored_engine.createLogModel(url));
  ASSERT_NE(restored_model, nullptr);
  EXPECT_EQ(restored_model->requestedScannerName(), QStringLiteral("Logfault"));
  restored_engine.releaseLogModel(url);
}

TEST(AppEngineTests, AppliesConfiguredDefaultScannerToNewSourcesWithoutMetadata) {
  ScopedTestSettings scoped_settings;
  UiSettings::instance().setDefaultLogScannerName(QStringLiteral("Logfault"));
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("default-format.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_GT(file.write("2026-03-31 10:15:00.123 host INFO 42 {worker} hello\n"), 0);
  file.close();

  AppEngine engine;
  auto* model = qobject_cast<LogModel*>(engine.createLogModel(QUrl::fromLocalFile(path)));
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->requestedScannerName(), QStringLiteral("Logfault"));
  EXPECT_EQ(model->scannerName(), QStringLiteral("Logfault"));
  engine.releaseLogModel(QUrl::fromLocalFile(path));
}

TEST(AppEngineTests, AppliesConfiguredDefaultWrapToNewSourcesWithoutMetadata) {
  ScopedTestSettings scoped_settings;
  UiSettings::instance().setWrapLogLinesByDefault(true);
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("default-wrap.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_GT(file.write("2026-03-31 10:15:00.123 INFO hello\n"), 0);
  file.close();

  AppEngine engine;
  const auto url = QUrl::fromLocalFile(path);
  EXPECT_TRUE(engine.wrapLogLinesForSource(url));
}

TEST(AppEngineTests, UsesGenericAsBuiltInDefaultScanner) {
  ScopedTestSettings scoped_settings;
  UiSettings::instance().setDefaultLogScannerName(QStringLiteral("Generic"));
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("generic-default.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_GT(file.write("2026-03-31 10:15:00.123 INFO hello\n continuation\n"), 0);
  file.close();

  AppEngine engine;
  auto* model = qobject_cast<LogModel*>(engine.createLogModel(QUrl::fromLocalFile(path)));
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(UiSettings::instance().defaultLogScannerName(), QStringLiteral("Generic"));
  EXPECT_EQ(model->requestedScannerName(), QStringLiteral("Generic"));
  EXPECT_EQ(model->scannerName(), QStringLiteral("Generic"));
  engine.releaseLogModel(QUrl::fromLocalFile(path));
}

TEST(AppEngineTests, ListsDistinctLogcatProcessesForCurrentSource) {
  ScopedTestSettings scoped_settings;
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("logcat.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_GT(file.write("04-02 16:12:43.821  1111  2222 I ActivityManager: booted\n"
                       "04-02 16:12:44.001  3333  4444 W MyApp: warning\n"
                       "04-02 16:12:44.101  1111  5555 D ActivityManager: again\n"),
            0);
  file.close();

  AppEngine engine;
  const auto url = QUrl::fromLocalFile(path);
  auto* model = qobject_cast<LogModel*>(engine.createLogModel(url));
  ASSERT_NE(model, nullptr);
  model->setRequestedScannerName(QStringLiteral("Logcat"));

  const auto processes = engine.logcatProcessesForSource(url);
  ASSERT_GE(processes.size(), 3);
  EXPECT_EQ(processes.at(0).toMap().value(QStringLiteral("pid")).toInt(), 0);
  EXPECT_EQ(processes.at(1).toMap().value(QStringLiteral("pid")).toInt(), 1111);
  EXPECT_EQ(processes.at(2).toMap().value(QStringLiteral("pid")).toInt(), 3333);
  engine.releaseLogModel(url);
}

TEST(AppEngineTests, ListsDistinctSystemdProcessesAlphabeticallyForCurrentSource) {
  ScopedTestSettings scoped_settings;
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("journal.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_GT(file.write("__REALTIME_TIMESTAMP=1712067163821000\tPRIORITY=6\t_COMM=zeta\tMESSAGE=last\n"
                       "__REALTIME_TIMESTAMP=1712067164821000\tPRIORITY=4\t_COMM=alpha\tMESSAGE=first\n"
                       "__REALTIME_TIMESTAMP=1712067165821000\tPRIORITY=6\tSYSLOG_IDENTIFIER=Beta\tMESSAGE=middle\n"
                       "__REALTIME_TIMESTAMP=1712067166821000\tPRIORITY=6\t_COMM=alpha\tMESSAGE=again\n"),
            0);
  file.close();

  AppEngine engine;
  const auto url = QUrl::fromLocalFile(path);
  auto* model = qobject_cast<LogModel*>(engine.createLogModel(url));
  ASSERT_NE(model, nullptr);
  model->setRequestedScannerName(QStringLiteral("Systemd"));

  const auto processes = engine.systemdProcessesForSource(url);
  ASSERT_EQ(processes.size(), 4);
  EXPECT_EQ(processes.at(0).toMap().value(QStringLiteral("name")).toString(), QString{});
  EXPECT_EQ(processes.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("alpha"));
  EXPECT_EQ(processes.at(2).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Beta"));
  EXPECT_EQ(processes.at(3).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("zeta"));
  engine.releaseLogModel(url);
}

TEST(AppEngineTests, SystemdJournalUrlPreservesStartAtNowMode) {
  const auto url = StreamSource::makeSystemdJournalUrl(QStringLiteral("glogg"), true);
  const auto spec = StreamSource::parseSystemdJournalSpec(url);
  ASSERT_TRUE(spec.has_value());
  EXPECT_EQ(spec->process_name, QStringLiteral("glogg"));
  EXPECT_TRUE(spec->start_at_now);
}

TEST(AppEngineTests, AppliesLogcatScannerToAdbLogcatSources) {
  ScopedTestSettings scoped_settings;
  UiSettings::instance().setDefaultLogScannerName(QStringLiteral("Generic"));

  AppEngine engine;
  const auto url = StreamSource::makeAdbLogcatUrl(QStringLiteral("ZX1G22B7"), QStringLiteral("Pixel"));
  auto* model = qobject_cast<LogModel*>(engine.createLogModel(url));
  ASSERT_NE(model, nullptr);

  EXPECT_EQ(model->requestedScannerName(), QStringLiteral("Logcat"));
  engine.releaseLogModel(url);
}

TEST(AppEngineTests, PersistsSourceWrapSettingAlongsideScannerMetadata) {
  ScopedTestSettings scoped_settings;
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("persist-wrap.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_GT(file.write("2026-03-31 10:15:00.123 INFO hello\n"), 0);
  file.close();

  const auto url = QUrl::fromLocalFile(path);

  {
    AppEngine engine;
    auto* model = qobject_cast<LogModel*>(engine.createLogModel(url));
    ASSERT_NE(model, nullptr);
    model->setRequestedScannerName(QStringLiteral("Logfault"));
    engine.saveWrapLogLinesForSource(url, true);
    engine.releaseLogModel(url);
  }

  AppEngine restored_engine;
  auto* restored_model = qobject_cast<LogModel*>(restored_engine.createLogModel(url));
  ASSERT_NE(restored_model, nullptr);
  EXPECT_EQ(restored_model->requestedScannerName(), QStringLiteral("Logfault"));
  EXPECT_TRUE(restored_engine.wrapLogLinesForSource(url));
  restored_engine.releaseLogModel(url);
}

TEST(AppEngineTests, OpenStreamCountTracksOpenStreamTabs) {
  ScopedTestSettings scoped_settings;
  AppEngine engine;

  EXPECT_EQ(engine.openStreamCount(), 0);
  EXPECT_GE(engine.openPipeStream(QStringLiteral("printf 'hello\\n'"), true, false, false), 0);
  EXPECT_EQ(engine.openStreamCount(), 1);
  EXPECT_GE(engine.openLogSource(QUrl(QStringLiteral("file:///tmp/plain.log"))), 0);
  EXPECT_EQ(engine.openStreamCount(), 1);
  EXPECT_TRUE(engine.closeOpenLogAt(0));
  EXPECT_EQ(engine.openStreamCount(), 0);
}

TEST(AppEngineTests, CleanCacheIsBlockedWhileStreamTabsAreOpen) {
  ScopedTestSettings scoped_settings;
  AppEngine engine;

  EXPECT_GE(engine.openPipeStream(QStringLiteral("printf 'hello\\n'"), true, false, false), 0);
  EXPECT_FALSE(engine.cleanCache());

  EXPECT_TRUE(engine.closeOpenLogAt(0));
  EXPECT_TRUE(engine.cleanCache());
}

TEST(AppEngineTests, SavedSourceScannerOverridesConfiguredDefaultScanner) {
  ScopedTestSettings scoped_settings;
  UiSettings::instance().setDefaultLogScannerName(QStringLiteral("Logcat"));
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("saved-format.log"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_GT(file.write("2026-03-31 10:15:00.123 host INFO 42 {worker} hello\n"), 0);
  file.close();

  const auto url = QUrl::fromLocalFile(path);
  {
    AppEngine engine;
    auto* model = qobject_cast<LogModel*>(engine.createLogModel(url));
    ASSERT_NE(model, nullptr);
    model->setRequestedScannerName(QStringLiteral("Logfault"));
    engine.releaseLogModel(url);
  }

  AppEngine restored_engine;
  auto* restored_model = qobject_cast<LogModel*>(restored_engine.createLogModel(url));
  ASSERT_NE(restored_model, nullptr);
  EXPECT_EQ(restored_model->requestedScannerName(), QStringLiteral("Logfault"));
  EXPECT_EQ(restored_model->scannerName(), QStringLiteral("Logfault"));
  restored_engine.releaseLogModel(url);
}

TEST(AppEngineTests, LimitsSourceFormatMetadataToOneHundredEntriesAndDeduplicates) {
  ScopedTestSettings scoped_settings;
  AppEngine engine;
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());

  for (int index = 0; index < 105; ++index) {
    const auto path = dir.filePath(QStringLiteral("metadata-%1.log").arg(index));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(file.write("line\n"), 0);
    file.close();

    const auto url = QUrl::fromLocalFile(path);
    auto* model = qobject_cast<LogModel*>(engine.createLogModel(url));
    ASSERT_NE(model, nullptr);
    model->setRequestedScannerName(QStringLiteral("None"));
    engine.releaseLogModel(url);
  }

  auto metadata_entries = QSettings{}.value("session/logSourceMetadata").toList();
  ASSERT_EQ(metadata_entries.size(), 100);
  EXPECT_EQ(metadata_entries.front().toMap().value("url").toString(),
            QUrl::fromLocalFile(dir.filePath(QStringLiteral("metadata-104.log"))).toString());
  EXPECT_EQ(metadata_entries.back().toMap().value("url").toString(),
            QUrl::fromLocalFile(dir.filePath(QStringLiteral("metadata-5.log"))).toString());

  const auto repeated_url = QUrl::fromLocalFile(dir.filePath(QStringLiteral("metadata-42.log")));
  auto* repeated_model = qobject_cast<LogModel*>(engine.createLogModel(repeated_url));
  ASSERT_NE(repeated_model, nullptr);
  repeated_model->setRequestedScannerName(QStringLiteral("Logfault"));
  engine.releaseLogModel(repeated_url);

  metadata_entries = QSettings{}.value("session/logSourceMetadata").toList();
  ASSERT_EQ(metadata_entries.size(), 100);
  EXPECT_EQ(metadata_entries.front().toMap().value("url").toString(),
            repeated_url.toString());
  EXPECT_EQ(metadata_entries.front().toMap().value("scannerName").toString(),
            QStringLiteral("Logfault"));

  int duplicate_count = 0;
  for (const auto& entry_value : metadata_entries) {
    if (entry_value.toMap().value("url").toString() == repeated_url.toString()) {
      ++duplicate_count;
    }
  }
  EXPECT_EQ(duplicate_count, 1);
}

}  // namespace
}  // namespace lgx
