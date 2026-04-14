#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "FileSource.h"
#include "PageCache.h"

namespace lgx {
namespace {

struct WindowRow {
  std::string raw_text;
  std::string function_name;
  std::string message;
  std::string thread_id;
  LogLevel log_level{LogLevel_Info};
  uint32_t pid{};
  uint32_t tid{};
  bool has_timestamp{false};
};

std::filesystem::path writeFile(const std::filesystem::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
  output.close();
  return path;
}

template <typename Predicate>
bool waitFor(Predicate&& predicate, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return predicate();
}

std::vector<WindowRow> readWindowRows(LogSource& source, uint64_t first_line, size_t count,
                                      bool raw = false) {
  std::vector<WindowRow> rows;
  const auto window = source.windowForSourceRange(first_line, count, raw);
  if (!window) {
    return rows;
  }

  for (const auto& line : window->lines_) {
    if (line.source_row < first_line || line.source_row >= first_line + count) {
      continue;
    }

    rows.push_back(WindowRow{
        .raw_text = std::string(window->rawText(line)),
        .function_name = std::string(window->functionNameText(line)),
        .message = std::string(window->messageText(line)),
        .thread_id = std::string(window->threadIdText(line)),
        .log_level = line.log_level,
        .pid = line.pid,
        .tid = line.tid,
        .has_timestamp = line.hasTimestamp(),
    });
  }

  return rows;
}

TEST(FileSourceTests, IndexesAndFetchesLinesFromFile) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(std::filesystem::path(dir.path().toStdString()) / "sample.log",
                              "alpha\nbeta\ngamma");

  FileSource source;
  source.open(path.string());
  source.startIndexing();

  const auto snapshot = source.snapshot();
  EXPECT_EQ(snapshot.state, SourceState::Ready);
  EXPECT_EQ(snapshot.line_count, 2U);
  EXPECT_EQ(snapshot.file_size, 16U);

  const auto rows = readWindowRows(source, 0, 3);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].raw_text, "alpha");
  EXPECT_EQ(rows[1].raw_text, "beta");
}

TEST(FileSourceTests, RefreshDetectsAppendAndFetchesNewLines) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(std::filesystem::path(dir.path().toStdString()) / "append.log",
                              "one\ntwo\n");

  FileSource source;
  uint64_t appended_first = 0;
  uint64_t appended_count = 0;
  source.setCallbacks(SourceCallbacks{
      .on_lines_appended =
          [&appended_first, &appended_count](uint64_t first_line, uint64_t count) {
            appended_first = first_line;
            appended_count = count;
          },
  });

  source.open(path.string());
  source.startIndexing();

  writeFile(path, "one\ntwo\nthree\nfour\n");
  source.refresh();

  const auto snapshot = source.snapshot();
  EXPECT_EQ(snapshot.state, SourceState::Ready);
  EXPECT_EQ(snapshot.line_count, 4U);
  EXPECT_EQ(appended_first, 2U);
  EXPECT_EQ(appended_count, 2U);

  const auto rows = readWindowRows(source, 2, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].raw_text, "three");
  EXPECT_EQ(rows[1].raw_text, "four");
}

TEST(FileSourceTests, FollowingWatcherRefreshesOnAppend) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path =
      writeFile(std::filesystem::path(dir.path().toStdString()) / "watch-append.log", "one\n");

  FileSource source;
  source.open(path.string());
  source.setFollowing(true);
  source.startIndexing();

  writeFile(path, "one\ntwo\n");
  ASSERT_TRUE(waitFor([&source] { return source.snapshot().line_count == 2U; }));

  const auto rows = readWindowRows(source, 0, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].raw_text, "one");
  EXPECT_EQ(rows[1].raw_text, "two");
}

TEST(FileSourceTests, FollowingEmptyFileDoesNotReportCatchingUp) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path =
      writeFile(std::filesystem::path(dir.path().toStdString()) / "empty-follow.log", "");

  FileSource source;
  source.open(path.string());
  source.setFollowing(true);
  source.startIndexing();

  const auto snapshot = source.snapshot();
  EXPECT_TRUE(snapshot.following);
  EXPECT_EQ(snapshot.file_size, 0U);
  EXPECT_EQ(snapshot.line_count, 0U);
  EXPECT_FALSE(snapshot.catching_up);
  EXPECT_EQ(snapshot.state, SourceState::Ready);
}

TEST(FileSourceTests, RefreshCompletesPendingTailWithoutRescanningCommittedLines) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(std::filesystem::path(dir.path().toStdString()) / "pending.log",
                              "one\ntwo\npar");

  FileSource source;
  uint64_t appended_first = 0;
  uint64_t appended_count = 0;
  source.setCallbacks(SourceCallbacks{
      .on_lines_appended =
          [&appended_first, &appended_count](uint64_t first_line, uint64_t count) {
            appended_first = first_line;
            appended_count = count;
          },
  });

  source.open(path.string());
  source.startIndexing();

  EXPECT_EQ(source.snapshot().line_count, 2U);

  writeFile(path, "one\ntwo\npartial");
  source.refresh();
  EXPECT_EQ(source.snapshot().line_count, 2U);
  EXPECT_EQ(appended_count, 0U);

  writeFile(path, "one\ntwo\npartial\nnext\n");
  source.refresh();

  EXPECT_EQ(source.snapshot().line_count, 4U);
  EXPECT_EQ(appended_first, 2U);
  EXPECT_EQ(appended_count, 2U);

  const auto rows = readWindowRows(source, 0, 4);
  ASSERT_EQ(rows.size(), 4U);
  EXPECT_EQ(rows[0].raw_text, "one");
  EXPECT_EQ(rows[1].raw_text, "two");
  EXPECT_EQ(rows[2].raw_text, "partial");
  EXPECT_EQ(rows[3].raw_text, "next");
}

TEST(FileSourceTests, RefreshDetectsTruncation) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(std::filesystem::path(dir.path().toStdString()) / "truncate.log",
                              "alpha\nbeta\n");

  FileSource source;
  SourceResetReason reset_reason = SourceResetReason::Disappeared;
  source.setCallbacks(SourceCallbacks{
      .on_reset =
          [&reset_reason](SourceResetReason reason) {
            reset_reason = reason;
          },
  });

  source.open(path.string());
  source.startIndexing();

  writeFile(path, "short\n");
  source.refresh();

  EXPECT_EQ(reset_reason, SourceResetReason::Truncated);
  EXPECT_EQ(source.snapshot().line_count, 1U);

  const auto rows = readWindowRows(source, 0, 1);
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows[0].raw_text, "short");
}

TEST(FileSourceTests, RefreshDetectsRecreatedFileIdentity) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(std::filesystem::path(dir.path().toStdString()) / "recreate.log",
                              "before\n");

  FileSource source;
  SourceResetReason reset_reason = SourceResetReason::Disappeared;
  source.setCallbacks(SourceCallbacks{
      .on_reset =
          [&reset_reason](SourceResetReason reason) {
            reset_reason = reason;
          },
  });

  source.open(path.string());
  source.startIndexing();

  std::filesystem::remove(path);
  writeFile(path, "after\n");
  source.refresh();

  EXPECT_EQ(reset_reason, SourceResetReason::Recreated);

  const auto rows = readWindowRows(source, 0, 1);
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows[0].raw_text, "after");
}

TEST(FileSourceTests, ResetInvalidatesAllCachedPages) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(std::filesystem::path(dir.path().toStdString()) / "cache-reset.log",
                              "alpha\nbeta\n");

  FileSource source;
  source.setRequestedScannerName("None");
  source.open(path.string());
  source.startIndexing();

  const auto rows = readWindowRows(source, 0, 1);
  ASSERT_EQ(rows.size(), 1U);

  const PageKey page_key{source.sourceId(), 0};
  EXPECT_TRUE(source.sharedPageCache().contains(page_key));

  writeFile(path, "short\n");
  source.refresh();

  EXPECT_FALSE(source.sharedPageCache().contains(page_key));
}

TEST(FileSourceTests, LogfaultScannerMergesMultilineEventsDuringIndexing) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "logfault-multiline.log",
      "2026-04-01 18:49:13.003 EEST TRACE 52208 Executing prepare-stmt SQL query: "
      "INSERT INTO request_state (userid, devid, instance, request_id)\n"
      "                    VALUES (?, ?, ?, ?)\n"
      "                    ON DUPLICATE KEY UPDATE\n"
      "                        request_id = VALUES(request_id),\n"
      "                        last_update = CURRENT_TIMESTAMP\n"
      "2026-04-01 18:49:13.003 EEST TRACE 52200 Executing stmt SQL query: "
      "INSERT INTO request_state (userid, devid, instance, request_id)\n"
      "                    VALUES (?, ?, ?, ?)\n"
      "                    ON DUPLICATE KEY UPDATE\n"
      "                        request_id = VALUES(request_id),\n"
      "                        last_update = CURRENT_TIMESTAMP | args: a, b, 1, 0\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Logfault");
  source.startIndexing();

  const auto snapshot = source.snapshot();
  EXPECT_EQ(snapshot.state, SourceState::Ready);
  EXPECT_EQ(snapshot.line_count, 2U);

  const auto rows = readWindowRows(source, 0, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].log_level, LogLevel_Trace);
  EXPECT_NE(rows[0].raw_text.find("VALUES (?, ?, ?, ?)"), std::string::npos);
  EXPECT_NE(rows[0].raw_text.find("last_update = CURRENT_TIMESTAMP"), std::string::npos);
  EXPECT_NE(rows[0].message.find("VALUES (?, ?, ?, ?)"), std::string::npos);
  EXPECT_NE(rows[0].message.find("last_update = CURRENT_TIMESTAMP"), std::string::npos);
  EXPECT_NE(rows[1].raw_text.find("| args: a, b, 1, 0"), std::string::npos);
}

TEST(FileSourceTests, LogfaultScannerExtendsPreviousEventWhenRefreshStartsWithContinuation) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "logfault-append.log",
      "2026-04-01 18:49:13.003 EEST TRACE 52208 Executing prepare-stmt SQL query: "
      "INSERT INTO request_state (userid, devid, instance, request_id)\n");

  FileSource source;
  uint64_t appended_count = 0;
  source.setCallbacks(SourceCallbacks{
      .on_lines_appended =
          [&appended_count](uint64_t, uint64_t count) {
            appended_count = count;
          },
  });

  source.open(path.string());
  source.setRequestedScannerName("Logfault");
  source.startIndexing();

  EXPECT_EQ(source.snapshot().line_count, 1U);

  writeFile(
      path,
      "2026-04-01 18:49:13.003 EEST TRACE 52208 Executing prepare-stmt SQL query: "
      "INSERT INTO request_state (userid, devid, instance, request_id)\n"
      "                    VALUES (?, ?, ?, ?)\n"
      "                    ON DUPLICATE KEY UPDATE\n"
      "                        last_update = CURRENT_TIMESTAMP\n"
      "2026-04-01 18:49:13.004 EEST INFO 52208 Done\n");
  source.refresh();

  EXPECT_EQ(source.snapshot().line_count, 2U);
  EXPECT_EQ(appended_count, 1U);

  const auto rows = readWindowRows(source, 0, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_NE(rows[0].raw_text.find("VALUES (?, ?, ?, ?)"), std::string::npos);
  EXPECT_NE(rows[0].raw_text.find("last_update = CURRENT_TIMESTAMP"), std::string::npos);
  EXPECT_EQ(rows[1].raw_text, "2026-04-01 18:49:13.004 EEST INFO 52208 Done");
}

TEST(FileSourceTests, GenericScannerRecognizesEarlyMixedCaseAndBracketedLevels) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "generic-levels.log",
      "2026-04-01 18:49:13.003 WarN first warning\n"
      "2026-04-01 18:49:13.004 [e] second error\n"
      "2026-04-01 18:49:13.005 [WarN][Note] third warning\n"
      "2026-04-01 18:49:13.006 debug fourth debug\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Generic");
  source.startIndexing();

  const auto snapshot = source.snapshot();
  EXPECT_EQ(snapshot.state, SourceState::Ready);
  EXPECT_EQ(snapshot.line_count, 4U);

  const auto rows = readWindowRows(source, 0, 4);
  ASSERT_EQ(rows.size(), 4U);
  EXPECT_EQ(rows[0].log_level, LogLevel_Warn);
  EXPECT_EQ(rows[1].log_level, LogLevel_Error);
  EXPECT_EQ(rows[2].log_level, LogLevel_Warn);
  EXPECT_EQ(rows[3].log_level, LogLevel_Debug);
}

TEST(FileSourceTests, GenericScannerTreatsIndentedLinesAsContinuations) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "generic-multiline.log",
      "2026-04-01 18:49:13.003 INFO start request\n"
      " stack trace line 1\n"
      " stack trace line 2\n"
      "2026-04-01 18:49:13.004 INFO done\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Generic");
  source.startIndexing();

  EXPECT_EQ(source.snapshot().line_count, 2U);

  const auto rows = readWindowRows(source, 0, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].log_level, LogLevel_Info);
  EXPECT_NE(rows[0].raw_text.find("stack trace line 1"), std::string::npos);
  EXPECT_NE(rows[0].raw_text.find("stack trace line 2"), std::string::npos);
  EXPECT_NE(rows[0].message.find("stack trace line 1"), std::string::npos);
  EXPECT_NE(rows[0].message.find("stack trace line 2"), std::string::npos);
  EXPECT_EQ(rows[1].raw_text, "2026-04-01 18:49:13.004 INFO done");
}

TEST(FileSourceTests, GenericScannerDoesNotMergeUnindentedJournalctlLines) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "generic-journalctl.log",
      "Hint: You are currently not seeing messages from other users and the system.\n"
      "Users in groups 'adm', 'systemd-journal', 'wheel' can see all messages.\n"
      "Pass -q to turn off this notice.\n"
      "Apr 02 21:31:12 archlinux kdeconnectd[2109]: Cannot find Bluez 5 adapter for device search false\n"
      "Apr 02 21:31:13 archlinux lgx[390666]: qrc:/qt/qml/lgx/qml/LogLineList.qml:513:17: QML "
      "QQuickRectangle*: Binding loop detected for property \"width\"\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Generic");
  source.startIndexing();

  EXPECT_EQ(source.snapshot().line_count, 5U);

  const auto rows = readWindowRows(source, 0, 5);
  ASSERT_EQ(rows.size(), 5U);
  EXPECT_EQ(rows[0].raw_text,
            "Hint: You are currently not seeing messages from other users and the system.");
  EXPECT_EQ(rows[1].raw_text,
            "Users in groups 'adm', 'systemd-journal', 'wheel' can see all messages.");
  EXPECT_EQ(rows[2].raw_text, "Pass -q to turn off this notice.");
  EXPECT_NE(rows[3].raw_text.find("Cannot find Bluez 5 adapter"), std::string::npos);
  EXPECT_NE(rows[4].raw_text.find("Binding loop detected"), std::string::npos);
}

TEST(FileSourceTests, GenericScannerDefaultsToInfoWithoutEarlyLevelMatch) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "generic-info-default.log",
      "2026-04-01 18:49:13.003 host=api request completed with error count 2\n"
      "2026-04-01 18:49:13.004 tenant=acme operation failed after timeout\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Generic");
  source.startIndexing();

  const auto rows = readWindowRows(source, 0, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].log_level, LogLevel_Info);
  EXPECT_EQ(rows[1].log_level, LogLevel_Info);
}

TEST(FileSourceTests, GenericScannerRecognizesDockerMariadbDualTimestampFormat) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "generic-mariadb.log",
      "2026-04-02T07:14:01.638754741Z 2026-04-02 07:14:01+00:00 [Note] [Entrypoint]: started\n"
      "2026-04-02T07:14:02.251932746Z 2026-04-02 7:14:02 0 [Warning] mariadbd: io_uring_queue_init() failed\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Generic");
  source.startIndexing();

  const auto rows = readWindowRows(source, 0, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].log_level, LogLevel_Notice);
  EXPECT_EQ(rows[1].log_level, LogLevel_Warn);
}

TEST(FileSourceTests, LogcatScannerPreservesPidAndNumericTid) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "logcat.log",
      "04-02 16:12:43.821  1234  5678 W MyAppTag: hello from android\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Logcat");
  source.startIndexing();

  const auto rows = readWindowRows(source, 0, 1);
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows[0].pid, 1234U);
  EXPECT_EQ(rows[0].tid, 5678U);
  EXPECT_EQ(rows[0].log_level, LogLevel_Warn);

  const auto pids = source.logcatPids();
  ASSERT_EQ(pids.size(), 1U);
  EXPECT_EQ(pids[0], 1234U);
}

TEST(FileSourceTests, SystemdScannerParsesStructuredJournalFields) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "journal.log",
      "__REALTIME_TIMESTAMP=1712067163821000\tPRIORITY=3\t_PID=1234\t_TID=5678\t_COMM=sshd\tMESSAGE=Failed password for root\n"
      "__REALTIME_TIMESTAMP=1712067164821000\tPRIORITY=6\tSYSLOG_IDENTIFIER=systemd\tMESSAGE=Started Session 12\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Systemd");
  source.startIndexing();

  const auto rows = readWindowRows(source, 0, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].pid, 1234U);
  EXPECT_EQ(rows[0].tid, 5678U);
  EXPECT_EQ(rows[0].function_name, "sshd");
  EXPECT_EQ(rows[0].message, "Failed password for root");
  EXPECT_EQ(rows[0].log_level, LogLevel_Error);
  EXPECT_TRUE(rows[0].has_timestamp);
  EXPECT_EQ(rows[1].function_name, "systemd");
  EXPECT_EQ(rows[1].message, "Started Session 12");
  EXPECT_EQ(rows[1].log_level, LogLevel_Info);

  const auto process_names = source.systemdProcessNames();
  ASSERT_EQ(process_names.size(), 2U);
  EXPECT_TRUE(std::find(process_names.begin(), process_names.end(), "sshd") != process_names.end());
  EXPECT_TRUE(std::find(process_names.begin(), process_names.end(), "systemd") != process_names.end());
}

TEST(FileSourceTests, SystemdScannerParsesFriendlyStreamMessages) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "systemd-friendly.log",
      "2026-03-27 04:22:39.259 WARN glogg[2137]: QFile::open: File (/tmp/nextapp-devel.log) already open\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Systemd");
  source.startIndexing();

  const auto rows = readWindowRows(source, 0, 1);
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows[0].pid, 2137U);
  EXPECT_EQ(rows[0].function_name, "glogg");
  EXPECT_EQ(rows[0].log_level, LogLevel_Warn);
  EXPECT_EQ(rows[0].message,
            "QFile::open: File (/tmp/nextapp-devel.log) already open");
  EXPECT_TRUE(rows[0].has_timestamp);
}

TEST(FileSourceTests, SystemdScannerParsesJournalctlShortMessages) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(
      std::filesystem::path(dir.path().toStdString()) / "journalctl.log",
      "Apr 02 16:12:43 host sudo[4321]: authentication failure\n");

  FileSource source;
  source.open(path.string());
  source.setRequestedScannerName("Systemd");
  source.startIndexing();

  const auto rows = readWindowRows(source, 0, 1);
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows[0].pid, 4321U);
  EXPECT_EQ(rows[0].function_name, "sudo");
  EXPECT_TRUE(rows[0].has_timestamp);
}

}  // namespace
}  // namespace lgx
