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
    QString raw_message;
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

  [[nodiscard]] std::shared_ptr<const SourceWindow> windowForSourceRange(
      uint64_t first_line, size_t count, bool raw) override {
    if (raw) {
      ++raw_line_view_calls;
    } else {
      ++line_view_calls;
    }

    auto window = std::make_shared<SourceWindow>();
    if (count == 0 || first_line >= entries_.size()) {
      return window;
    }

    const auto last_line =
        std::min<uint64_t>(entries_.size(), first_line + count);
    window->first_source_row_ = first_line;
    window->last_source_row_ = last_line;
    window->pages_.reserve(static_cast<size_t>(last_line - first_line));
    window->lines_.reserve(static_cast<size_t>(last_line - first_line));

    for (uint64_t line_number = first_line; line_number < last_line; ++line_number) {
      const auto& entry = entries_.at(static_cast<size_t>(line_number));
      std::string raw_text = entry.raw_message.toStdString();
      std::string full_text = raw_text;
      TextSpan function_name;
      TextSpan message;
      if (!raw && !entry.function_name.isEmpty()) {
        full_text = entry.function_name.toStdString();
        full_text += ": ";
        const auto message_offset = static_cast<int32_t>(full_text.size());
        full_text += entry.message.toStdString();
        function_name = TextSpan{.offset = 0,
                                 .length = static_cast<uint32_t>(entry.function_name.size())};
        message = TextSpan{.offset = message_offset,
                           .length = static_cast<uint32_t>(entry.message.toStdString().size())};
      } else if (!raw && !entry.message.isEmpty()) {
        full_text = entry.message.toStdString();
        message = TextSpan{.offset = 0,
                           .length = static_cast<uint32_t>(entry.message.toStdString().size())};
      }

      const auto page_slot = static_cast<uint32_t>(window->pages_.size());
      window->pages_.push_back(PageData::fromText((raw ? raw_text : full_text) + '\n',
                                                  {entry.level}));
      window->lines_.push_back(SourceWindowLine{
          .source_row = line_number,
          .log_level = entry.level,
          .pid = static_cast<uint32_t>(entry.pid),
          .tid = static_cast<uint32_t>(entry.tid),
          .function_name = function_name,
          .message = message,
          .thread_id = {},
          .timestamp_msecs_since_epoch = -1,
          .page_slot = page_slot,
          .line_index_in_page = 0,
      });
    }

    return window;
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
  mutable int next_line_calls{0};
  mutable int previous_line_calls{0};
  mutable int line_view_calls{0};
  mutable int raw_line_view_calls{0};

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
      {.level = LogLevel_Info, .raw_message = QStringLiteral("first"), .message = QStringLiteral("first")},
      {.level = LogLevel_Warn, .raw_message = QStringLiteral("second"), .message = QStringLiteral("second")},
      {.level = LogLevel_Error, .raw_message = QStringLiteral("third"), .message = QStringLiteral("third")},
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
      {.level = LogLevel_Warn, .pid = 1111, .raw_message = QStringLiteral("first"), .message = QStringLiteral("first")},
      {.level = LogLevel_Warn, .pid = 2222, .raw_message = QStringLiteral("second"), .message = QStringLiteral("second")},
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

TEST(FilterModelTests, RawTextSearchMatchesWholeLineWhileDefaultUsesMessageOnly) {
  auto source = std::make_unique<CountingLogSource>(std::vector<CountingLogSource::Entry>{
      {.level = LogLevel_Info,
       .raw_message = QStringLiteral("2026-04-12 INFO worker: hidden payload"),
       .message = QStringLiteral("hidden payload")},
      {.level = LogLevel_Info,
       .raw_message = QStringLiteral("2026-04-12 INFO helper: other"),
       .message = QStringLiteral("other")},
  });
  auto* source_ptr = source.get();

  LogModel source_model(QUrl(QStringLiteral("file:///tmp/raw-search.log")));
  source_model.setSource(std::move(source));
  source_model.loadFromSource();

  FilterModel filter_model(&source_model);
  filter_model.setPattern(QStringLiteral("worker"));
  filter_model.refresh();

  EXPECT_EQ(filter_model.rowCount(), 0);

  filter_model.setRaw(true);
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 1);
  EXPECT_EQ(filter_model.plainTextAt(0), QStringLiteral("hidden payload"));
  EXPECT_GT(source_ptr->raw_line_view_calls, 0);
}

TEST(FilterModelTests, RawTextAndLevelFilteringUsesRawLineViewsWithoutDeepParsing) {
  auto source = std::make_unique<CountingLogSource>(std::vector<CountingLogSource::Entry>{
      {.level = LogLevel_Info,
       .raw_message = QStringLiteral("INFO worker alpha"),
       .message = QStringLiteral("alpha")},
      {.level = LogLevel_Debug,
       .raw_message = QStringLiteral("DEBUG worker beta"),
       .message = QStringLiteral("beta")},
      {.level = LogLevel_Warn,
       .raw_message = QStringLiteral("WARN helper worker"),
       .message = QStringLiteral("gamma")},
  });
  auto* source_ptr = source.get();

  LogModel source_model(QUrl(QStringLiteral("file:///tmp/raw-level-search.log")));
  source_model.setSource(std::move(source));
  source_model.loadFromSource();

  FilterModel filter_model(&source_model);
  for (int level = static_cast<int>(LogLevel_Error); level <= static_cast<int>(LogLevel_Trace); ++level) {
    filter_model.setLevelEnabled(level, false);
  }
  filter_model.setRaw(true);
  filter_model.setPattern(QStringLiteral("worker"));
  filter_model.setLevelEnabled(static_cast<int>(LogLevel_Warn), true);
  filter_model.refresh();

  ASSERT_EQ(filter_model.rowCount(), 1);
  EXPECT_EQ(filter_model.sourceRowAt(0), 2);
  EXPECT_EQ(source_ptr->line_view_calls, 0);
  EXPECT_GT(source_ptr->raw_line_view_calls, 0);
}

TEST(FilterModelTests, LogModelExposesSourceBackedRowCountWithoutFetchingRows) {
  LogModel source_model(QUrl(QStringLiteral("file:///tmp/filter.log")));

  auto source = std::make_unique<CountingLogSource>(std::vector<CountingLogSource::Entry>{
      {.level = LogLevel_Info, .raw_message = QStringLiteral("first"), .message = QStringLiteral("first")},
      {.level = LogLevel_Warn, .raw_message = QStringLiteral("second"), .message = QStringLiteral("second")},
  });
  auto* source_ptr = source.get();
  source_model.setSource(std::move(source));
  source_model.loadFromSource();

  EXPECT_EQ(source_ptr->line_view_calls, 0);
  EXPECT_EQ(source_model.rowCount(), 2);

  const auto index = source_model.index(0, 0);
  ASSERT_TRUE(index.isValid());
  EXPECT_EQ(source_model.data(index, LogModel::MessageRole).toString(), QStringLiteral("first"));
  EXPECT_EQ(source_ptr->line_view_calls, 1);

  source_model.setCurrent(true);
  EXPECT_EQ(source_model.plainTextAt(1), QStringLiteral("second"));
  EXPECT_EQ(source_ptr->line_view_calls, 2);
}

}  // namespace
}  // namespace lgx
