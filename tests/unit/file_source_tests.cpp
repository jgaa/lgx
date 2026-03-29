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
  source.open(path.string());
  source.startIndexing();

  SourceLines fetched;
  source.fetchLines(0, 2, [&fetched](SourceLines lines) { fetched = std::move(lines); });
  ASSERT_EQ(fetched.lines.size(), 2U);

  const PageKey page_key{source.sourceId(), 0};
  EXPECT_TRUE(source.sharedPageCache().contains(page_key));

  writeFile(path, "short\n");
  source.refresh();

  EXPECT_FALSE(source.sharedPageCache().contains(page_key));
}

}  // namespace
}  // namespace lgx
