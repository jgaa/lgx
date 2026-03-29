#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>

#include "AppEngine.h"
#include "FileSource.h"

namespace lgx {
namespace {

class ScopedTestSettings {
 public:
  ScopedTestSettings()
      : temp_dir_() {
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
  EXPECT_EQ(model.data(index, LogModel::RawMessageRole).toString(), QStringLiteral("[debug] raw"));
  EXPECT_EQ(model.data(index, LogModel::MessageRole).toString(), QStringLiteral("formatted"));
  EXPECT_EQ(model.data(index, LogModel::DateRole).toDateTime(), row.date);
  EXPECT_EQ(model.data(index, LogModel::TagsRole).toStringList(), row.tags);
  EXPECT_EQ(model.data(index, LogModel::ThreadIdRole).toString(), QStringLiteral("thread-1"));

  const auto roles = model.roleNames();
  EXPECT_EQ(roles.value(LogModel::LineNoRole), QByteArray("lineNo"));
  EXPECT_EQ(roles.value(LogModel::FunctionNameRole), QByteArray("functionName"));
  EXPECT_EQ(roles.value(LogModel::LogLevelRole), QByteArray("logLevel"));
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

}  // namespace
}  // namespace lgx
