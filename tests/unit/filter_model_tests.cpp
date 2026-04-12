#include <gtest/gtest.h>

#include <vector>

#include "FilterModel.h"
#include "LogSource.h"

namespace lgx {
namespace {

class CountingLogSource final : public LogSource {
 public:
  struct Entry {
    LogLevel level{LogLevel_Info};
    qint64 pid{};
    qint64 tid{};
    QString function_name;
    QString message;
  };

  explicit CountingLogSource(std::vector<Entry> entries)
      : entries_(std::move(entries)) {
    setLines(entries_.size());
  }

  [[nodiscard]] SourceSnapshot snapshot() const override {
    return SourceSnapshot{
        .state = SourceState::Ready,
        .line_count = static_cast<uint64_t>(entries_.size()),
        .indexed_size = static_cast<uint64_t>(entries_.size()),
        .file_size = static_cast<uint64_t>(entries_.size()),
        .following = false,
        .lines_per_second = 0.0,
    };
  }

  void startIndexing() override {
    emitStateChanged(snapshot());
  }

  void fetchLines(uint64_t first_line, size_t count,
                  std::function<void(SourceLines)> on_ready) override {
    ++fetch_lines_calls;
    SourceLines lines;
    lines.first_line = first_line;
    if (first_line < entries_.size() && count > 0) {
      const auto actual = std::min(count, entries_.size() - static_cast<size_t>(first_line));
      lines.lines.reserve(actual);
      for (size_t index = 0; index < actual; ++index) {
        const auto& entry = entries_.at(static_cast<size_t>(first_line) + index);
        lines.lines.push_back(SourceLine{
            .line_number = first_line + index,
            .log_level = entry.level,
            .pid = static_cast<uint32_t>(entry.pid),
            .tid = static_cast<uint32_t>(entry.tid),
            .text = entry.message.toStdString(),
            .function_name = entry.function_name.toStdString(),
            .message = entry.message.toStdString(),
        });
      }
    }

    if (on_ready) {
      on_ready(std::move(lines));
    }
  }

  [[nodiscard]] std::optional<uint64_t> nextLineWithLevel(uint64_t after_line,
                                                          LogLevel level) const override {
    ++next_line_calls;
    const auto start = std::max<uint64_t>(after_line + 1, 0);
    for (size_t index = static_cast<size_t>(start); index < entries_.size(); ++index) {
      if (entries_[index].level == level) {
        return static_cast<uint64_t>(index);
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<uint64_t> previousLineWithLevel(uint64_t before_line,
                                                              LogLevel level) const override {
    ++previous_line_calls;
    if (entries_.empty()) {
      return std::nullopt;
    }

    auto index = std::min<uint64_t>(before_line, entries_.size() - 1);
    while (true) {
      if (entries_[static_cast<size_t>(index)].level == level) {
        return index;
      }
      if (index == 0) {
        break;
      }
      --index;
    }
    return std::nullopt;
  }

  mutable int fetch_lines_calls{0};
  mutable int next_line_calls{0};
  mutable int previous_line_calls{0};

 private:
  std::vector<Entry> entries_;
};

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

TEST(FilterModelTests, PureLevelFilteringUsesSourceLevelNavigationWithoutFetchingLines) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/filter.log")));

  LogRow first;
  first.line_no = 1;
  first.log_level = LogLevel_Info;
  first.message = QStringLiteral("first");
  first.raw_message = QStringLiteral("first");

  LogRow second;
  second.line_no = 2;
  second.log_level = LogLevel_Warn;
  second.message = QStringLiteral("second");
  second.raw_message = QStringLiteral("second");

  LogRow third;
  third.line_no = 3;
  third.log_level = LogLevel_Error;
  third.message = QStringLiteral("third");
  third.raw_message = QStringLiteral("third");

  auto source = std::make_unique<CountingLogSource>(std::vector<CountingLogSource::Entry>{
      {.level = LogLevel_Info, .message = QStringLiteral("first")},
      {.level = LogLevel_Warn, .message = QStringLiteral("second")},
      {.level = LogLevel_Error, .message = QStringLiteral("third")},
  });
  auto* source_ptr = source.get();
  source_model.setRows({first, second, third});
  source_model.setSource(std::move(source));
  source_model.markReady();

  FilterModel filter_model(&source_model);
  for (int level = static_cast<int>(LogLevel_Error); level <= static_cast<int>(LogLevel_Trace); ++level) {
    filter_model.setLevelEnabled(level, false);
  }
  filter_model.setLevelEnabled(static_cast<int>(LogLevel_Warn), true);
  filter_model.setLevelEnabled(static_cast<int>(LogLevel_Error), true);
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 2);
  EXPECT_EQ(filter_model.sourceRowAt(0), 1);
  EXPECT_EQ(filter_model.sourceRowAt(1), 2);
  EXPECT_GT(source_ptr->next_line_calls, 0);
  EXPECT_EQ(source_ptr->fetch_lines_calls, 0);
}

TEST(FilterModelTests, MixedPidAndLevelFilteringDoesNotUseSourceLevelNavigationShortcut) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/filter.log")));

  LogRow first;
  first.line_no = 1;
  first.pid = 1111;
  first.log_level = LogLevel_Warn;
  first.message = QStringLiteral("first");
  first.raw_message = QStringLiteral("first");

  LogRow second;
  second.line_no = 2;
  second.pid = 2222;
  second.log_level = LogLevel_Warn;
  second.message = QStringLiteral("second");
  second.raw_message = QStringLiteral("second");

  auto source = std::make_unique<CountingLogSource>(std::vector<CountingLogSource::Entry>{
      {.level = LogLevel_Warn, .pid = 1111, .message = QStringLiteral("first")},
      {.level = LogLevel_Warn, .pid = 2222, .message = QStringLiteral("second")},
  });
  auto* source_ptr = source.get();
  source_model.setRows({first, second});
  source_model.setSource(std::move(source));
  source_model.markReady();

  FilterModel filter_model(&source_model);
  for (int level = static_cast<int>(LogLevel_Error); level <= static_cast<int>(LogLevel_Trace); ++level) {
    filter_model.setLevelEnabled(level, false);
  }
  filter_model.setLevelEnabled(static_cast<int>(LogLevel_Warn), true);
  filter_model.setSelectedPid(2222);
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 1);
  EXPECT_EQ(filter_model.sourceRowAt(0), 1);
  EXPECT_EQ(source_ptr->next_line_calls, 0);
}

TEST(FilterModelTests, RepeatedTextFilteringProducesStableResults) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/filter.log")));

  LogRow first;
  first.line_no = 1;
  first.function_name = QStringLiteral("worker");
  first.message = QStringLiteral("alpha payload");
  first.raw_message = QStringLiteral("alpha payload");

  LogRow second;
  second.line_no = 2;
  second.function_name = QStringLiteral("dispatcher");
  second.message = QStringLiteral("beta payload");
  second.raw_message = QStringLiteral("beta payload");

  LogRow third;
  third.line_no = 3;
  third.function_name = QStringLiteral("worker");
  third.message = QStringLiteral("gamma payload");
  third.raw_message = QStringLiteral("gamma payload");

  source_model.setRows({first, second, third});
  source_model.markReady();

  FilterModel filter_model(&source_model);
  filter_model.setPattern(QStringLiteral("payload"));
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 3);
  EXPECT_EQ(filter_model.sourceRowAt(0), 0);
  EXPECT_EQ(filter_model.sourceRowAt(1), 1);
  EXPECT_EQ(filter_model.sourceRowAt(2), 2);

  filter_model.setPattern(QStringLiteral("worker"));
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 0);

  filter_model.setPattern(QStringLiteral("payload"));
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 3);
  EXPECT_EQ(filter_model.plainTextAt(0), QStringLiteral("alpha payload"));
  EXPECT_EQ(filter_model.plainTextAt(1), QStringLiteral("beta payload"));
  EXPECT_EQ(filter_model.plainTextAt(2), QStringLiteral("gamma payload"));
}

TEST(FilterModelTests, LogModelExposesSourceBackedRowCountWithoutFetchingRows) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/filter.log")));

  auto source = std::make_unique<CountingLogSource>(std::vector<CountingLogSource::Entry>{
      {.level = LogLevel_Info, .message = QStringLiteral("first")},
      {.level = LogLevel_Warn, .message = QStringLiteral("second")},
  });
  auto* source_ptr = source.get();
  source_model.setSource(std::move(source));
  source_model.loadFromSource();

  EXPECT_EQ(source_ptr->fetch_lines_calls, 0);
  EXPECT_EQ(source_model.rowCount(), 2);

  const auto index = source_model.index(0, 0);
  ASSERT_TRUE(index.isValid());
  EXPECT_EQ(source_model.data(index, LogModel::MessageRole).toString(), QStringLiteral("first"));
  EXPECT_EQ(source_ptr->fetch_lines_calls, 1);

  source_model.setCurrent(true);
  EXPECT_EQ(source_model.plainTextAt(1), QStringLiteral("second"));
  EXPECT_EQ(source_ptr->fetch_lines_calls, 2);
}

}  // namespace
}  // namespace lgx
