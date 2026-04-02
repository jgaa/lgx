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

  SourceLines lines;
  source.fetchLines(0, 3, [&lines](SourceLines fetched) { lines = std::move(fetched); });

  ASSERT_EQ(lines.lines.size(), 2U);
  EXPECT_EQ(lines.lines[0].text, "alpha");
  EXPECT_EQ(lines.lines[1].text, "beta");
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

  SourceLines fetched;
  source.fetchLines(2, 2, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 2U);
  EXPECT_EQ(fetched.lines[0].text, "three");
  EXPECT_EQ(fetched.lines[1].text, "four");
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

  SourceLines fetched;
  source.fetchLines(0, 2, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 2U);
  EXPECT_EQ(fetched.lines[0].text, "one");
  EXPECT_EQ(fetched.lines[1].text, "two");
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

  SourceLines fetched;
  source.fetchLines(0, 4, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 4U);
  EXPECT_EQ(fetched.lines[0].text, "one");
  EXPECT_EQ(fetched.lines[1].text, "two");
  EXPECT_EQ(fetched.lines[2].text, "partial");
  EXPECT_EQ(fetched.lines[3].text, "next");
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

  SourceLines fetched;
  source.fetchLines(0, 1, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 1U);
  EXPECT_EQ(fetched.lines[0].text, "short");
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

  SourceLines fetched;
  source.fetchLines(0, 1, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 1U);
  EXPECT_EQ(fetched.lines[0].text, "after");
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

  SourceLines fetched;
  source.fetchLines(0, 1, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 1U);

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

  SourceLines fetched;
  source.fetchLines(0, 2, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 2U);
  EXPECT_EQ(fetched.lines[0].log_level, LogLevel_Trace);
  EXPECT_NE(fetched.lines[0].text.find("VALUES (?, ?, ?, ?)"), std::string::npos);
  EXPECT_NE(fetched.lines[0].text.find("last_update = CURRENT_TIMESTAMP"), std::string::npos);
  EXPECT_NE(fetched.lines[1].text.find("| args: a, b, 1, 0"), std::string::npos);
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

  SourceLines fetched;
  source.fetchLines(0, 2, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 2U);
  EXPECT_NE(fetched.lines[0].text.find("VALUES (?, ?, ?, ?)"), std::string::npos);
  EXPECT_NE(fetched.lines[0].text.find("last_update = CURRENT_TIMESTAMP"), std::string::npos);
  EXPECT_EQ(fetched.lines[1].text, "2026-04-01 18:49:13.004 EEST INFO 52208 Done");
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

  SourceLines fetched;
  source.fetchLines(0, 4, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 4U);
  EXPECT_EQ(fetched.lines[0].log_level, LogLevel_Warn);
  EXPECT_EQ(fetched.lines[1].log_level, LogLevel_Error);
  EXPECT_EQ(fetched.lines[2].log_level, LogLevel_Warn);
  EXPECT_EQ(fetched.lines[3].log_level, LogLevel_Debug);
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

  SourceLines fetched;
  source.fetchLines(0, 2, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 2U);
  EXPECT_EQ(fetched.lines[0].log_level, LogLevel_Info);
  EXPECT_NE(fetched.lines[0].text.find("stack trace line 1"), std::string::npos);
  EXPECT_NE(fetched.lines[0].text.find("stack trace line 2"), std::string::npos);
  EXPECT_EQ(fetched.lines[1].text, "2026-04-01 18:49:13.004 INFO done");
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

  SourceLines fetched;
  source.fetchLines(0, 2, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 2U);
  EXPECT_EQ(fetched.lines[0].log_level, LogLevel_Info);
  EXPECT_EQ(fetched.lines[1].log_level, LogLevel_Info);
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

  SourceLines fetched;
  source.fetchLines(0, 2, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 2U);
  EXPECT_EQ(fetched.lines[0].log_level, LogLevel_Notice);
  EXPECT_EQ(fetched.lines[1].log_level, LogLevel_Warn);
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

  SourceLines fetched;
  source.fetchLines(0, 1, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 1U);
  EXPECT_EQ(fetched.lines[0].pid, 1234U);
  EXPECT_EQ(fetched.lines[0].tid, 5678U);
  EXPECT_EQ(fetched.lines[0].log_level, LogLevel_Warn);
}

}  // namespace
}  // namespace lgx
