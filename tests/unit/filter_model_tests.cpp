#include <gtest/gtest.h>

#include "FilterModel.h"

namespace lgx {
namespace {

TEST(FilterModelTests, FiltersBySelectedPid) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/logcat.log")));

  LogRow first;
  first.line_no = 1;
  first.pid = 1111;
  first.tid = 2222;
  first.raw_message = QStringLiteral("first");
  first.message = QStringLiteral("first");

  LogRow second;
  second.line_no = 2;
  second.pid = 3333;
  second.tid = 4444;
  second.raw_message = QStringLiteral("second");
  second.message = QStringLiteral("second");

  source_model.setRows({first, second});
  source_model.markReady();

  FilterModel filter_model(&source_model);
  filter_model.setSelectedPid(3333);
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 1);
  EXPECT_EQ(filter_model.pidAt(0), 3333);
  EXPECT_EQ(filter_model.tidAt(0), 4444);
}

TEST(FilterModelTests, ShowsAllRowsAfterClearingSelectedPid) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/logcat.log")));

  LogRow first;
  first.line_no = 1;
  first.pid = 1111;
  first.raw_message = QStringLiteral("first");
  first.message = QStringLiteral("first");

  LogRow second;
  second.line_no = 2;
  second.pid = 3333;
  second.raw_message = QStringLiteral("second");
  second.message = QStringLiteral("second");

  source_model.setRows({first, second});
  source_model.markReady();

  FilterModel filter_model(&source_model);
  filter_model.setSelectedPid(3333);
  filter_model.refresh();
  ASSERT_EQ(filter_model.rowCount(), 1);

  filter_model.setSelectedPid(0);
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 2);
  EXPECT_EQ(filter_model.plainTextAt(0), QStringLiteral("first"));
  EXPECT_EQ(filter_model.plainTextAt(1), QStringLiteral("second"));
}

TEST(FilterModelTests, FiltersBySelectedProcessName) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/journal.log")));

  LogRow first;
  first.line_no = 1;
  first.function_name = QStringLiteral("sshd");
  first.raw_message = QStringLiteral("first");
  first.message = QStringLiteral("first");

  LogRow second;
  second.line_no = 2;
  second.function_name = QStringLiteral("systemd");
  second.raw_message = QStringLiteral("second");
  second.message = QStringLiteral("second");

  source_model.setRows({first, second});
  source_model.markReady();

  FilterModel filter_model(&source_model);
  filter_model.setSelectedProcessName(QStringLiteral("systemd"));
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 1);
  EXPECT_EQ(filter_model.plainTextAt(0), QStringLiteral("second"));
}

TEST(FilterModelTests, ShowsAllRowsAfterClearingSelectedProcessName) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/journal.log")));

  LogRow first;
  first.line_no = 1;
  first.function_name = QStringLiteral("sshd");
  first.raw_message = QStringLiteral("first");
  first.message = QStringLiteral("first");

  LogRow second;
  second.line_no = 2;
  second.function_name = QStringLiteral("systemd");
  second.raw_message = QStringLiteral("second");
  second.message = QStringLiteral("second");

  source_model.setRows({first, second});
  source_model.markReady();

  FilterModel filter_model(&source_model);
  filter_model.setSelectedProcessName(QStringLiteral("systemd"));
  filter_model.refresh();
  ASSERT_EQ(filter_model.rowCount(), 1);

  filter_model.setSelectedProcessName(QString{});
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 2);
  EXPECT_EQ(filter_model.plainTextAt(0), QStringLiteral("first"));
  EXPECT_EQ(filter_model.plainTextAt(1), QStringLiteral("second"));
}

TEST(FilterModelTests, ListsSystemdProcessesAlphabeticallyFromSourceModel) {
  LogModel source_model(QUrl(QStringLiteral("systemd:/journal")));

  LogRow first;
  first.line_no = 1;
  first.function_name = QStringLiteral("zeta");
  first.raw_message = QStringLiteral("first");
  first.message = QStringLiteral("first");

  LogRow second;
  second.line_no = 2;
  second.function_name = QStringLiteral("alpha");
  second.raw_message = QStringLiteral("second");
  second.message = QStringLiteral("second");

  LogRow third;
  third.line_no = 3;
  third.function_name = QStringLiteral("Beta");
  third.raw_message = QStringLiteral("third");
  third.message = QStringLiteral("third");

  LogRow fourth;
  fourth.line_no = 4;
  fourth.function_name = QStringLiteral("alpha");
  fourth.raw_message = QStringLiteral("fourth");
  fourth.message = QStringLiteral("fourth");

  source_model.setRows({first, second, third, fourth});
  source_model.markReady();

  FilterModel filter_model(&source_model);
  const auto processes = filter_model.systemdProcesses();

  ASSERT_EQ(processes.size(), 4);
  EXPECT_EQ(processes.at(0).toMap().value(QStringLiteral("name")).toString(), QString{});
  EXPECT_EQ(processes.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("alpha"));
  EXPECT_EQ(processes.at(2).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Beta"));
  EXPECT_EQ(processes.at(3).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("zeta"));
}

TEST(FilterModelTests, CombinesSelectedPidWithEnabledLevels) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/logcat.log")));

  LogRow first;
  first.line_no = 1;
  first.pid = 1111;
  first.tid = 2222;
  first.log_level = LogLevel_Debug;
  first.raw_message = QStringLiteral("other-process-debug");
  first.message = QStringLiteral("other-process-debug");

  LogRow second;
  second.line_no = 2;
  second.pid = 3333;
  second.tid = 4444;
  second.log_level = LogLevel_Debug;
  second.raw_message = QStringLiteral("my-process-debug");
  second.message = QStringLiteral("my-process-debug");

  LogRow third;
  third.line_no = 3;
  third.pid = 3333;
  third.tid = 4445;
  third.log_level = LogLevel_Info;
  third.raw_message = QStringLiteral("my-process-info");
  third.message = QStringLiteral("my-process-info");

  source_model.setRows({first, second, third});
  source_model.markReady();

  FilterModel filter_model(&source_model);
  for (int level = static_cast<int>(LogLevel_Error); level <= static_cast<int>(LogLevel_Trace); ++level) {
    filter_model.setLevelEnabled(level, false);
  }
  filter_model.setLevelEnabled(static_cast<int>(LogLevel_Debug), true);
  filter_model.setSelectedPid(3333);
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 1);
  EXPECT_EQ(filter_model.pidAt(0), 3333);
  EXPECT_EQ(filter_model.tidAt(0), 4444);
  EXPECT_EQ(filter_model.logLevelAt(0), static_cast<int>(LogLevel_Debug));
}

TEST(FilterModelTests, FindsProxyRowAtOrAfterSourceRow) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/filter.log")));

  LogRow first;
  first.line_no = 1;
  first.raw_message = QStringLiteral("first");
  first.message = QStringLiteral("first");

  LogRow second;
  second.line_no = 2;
  second.log_level = LogLevel_Warn;
  second.raw_message = QStringLiteral("second");
  second.message = QStringLiteral("second");

  LogRow third;
  third.line_no = 3;
  third.raw_message = QStringLiteral("third");
  third.message = QStringLiteral("third");

  LogRow fourth;
  fourth.line_no = 4;
  fourth.log_level = LogLevel_Warn;
  fourth.raw_message = QStringLiteral("fourth");
  fourth.message = QStringLiteral("fourth");

  source_model.setRows({first, second, third, fourth});
  source_model.markReady();

  FilterModel filter_model(&source_model);
  for (int level = static_cast<int>(LogLevel_Error); level <= static_cast<int>(LogLevel_Trace); ++level) {
    filter_model.setLevelEnabled(level, false);
  }
  filter_model.setLevelEnabled(static_cast<int>(LogLevel_Warn), true);
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 2);
  EXPECT_EQ(filter_model.proxyRowAtOrAfterSourceRow(0), 0);
  EXPECT_EQ(filter_model.proxyRowAtOrAfterSourceRow(1), 0);
  EXPECT_EQ(filter_model.proxyRowAtOrAfterSourceRow(2), 1);
  EXPECT_EQ(filter_model.proxyRowAtOrAfterSourceRow(3), 1);
  EXPECT_EQ(filter_model.proxyRowAtOrAfterSourceRow(4), 1);
}

}  // namespace
}  // namespace lgx
