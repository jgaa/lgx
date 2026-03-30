#include "LogModel.h"

#include <algorithm>

#include <QDateTime>

namespace lgx {

namespace {

LogRow makeRow(const SourceLine& line) {
  return LogRow{
      .line_no = static_cast<qsizetype>(line.line_number + 1U),
      .function_name = QString::fromStdString(line.function_name),
      .log_level = line.log_level,
      .raw_message = QString::fromStdString(line.text),
      .message = QString::fromStdString(line.message.empty() ? line.text : line.message),
      .date = line.timestamp ? QDateTime::fromMSecsSinceEpoch(
                                   std::chrono::duration_cast<std::chrono::milliseconds>(
                                       line.timestamp->time_since_epoch())
                                       .count())
                             : QDateTime{},
      .thread_id = QString::fromStdString(line.thread_id),
  };
}

}  // namespace

LogModel::LogModel(QUrl source_url, QObject* parent)
    : QAbstractListModel(parent),
      source_url_(std::move(source_url)) {
  active_timer_.setSingleShot(true);
  active_timer_.setInterval(10'000);
  connect(&active_timer_, &QTimer::timeout, this, [this]() {
    setActive(false);
  });

  source_metrics_timer_.setInterval(1'000);
  connect(&source_metrics_timer_, &QTimer::timeout, this, [this]() {
    refreshSourceMetrics();
  });
}

int LogModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(rows_.size());
}

QVariant LogModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  const auto& row = rows_[static_cast<size_t>(index.row())];
  switch (role) {
    case SourceRowRole:
      return index.row();
    case LineNoRole:
      return QVariant::fromValue(row.line_no);
    case FunctionNameRole:
      return row.function_name;
    case LogLevelRole:
      return QVariant::fromValue(static_cast<int>(row.log_level));
    case MarkedRole:
      return row.mark_color != LineMark_None;
    case MarkColorRole:
      return QVariant::fromValue(static_cast<int>(row.mark_color));
    case RawMessageRole:
      return row.raw_message;
    case MessageRole:
      return row.message;
    case DateRole:
      return row.date;
    case TagsRole:
      return row.tags;
    case ThreadIdRole:
      return row.thread_id;
    default:
      return {};
  }
}

QHash<int, QByteArray> LogModel::roleNames() const {
  return {
      {SourceRowRole, "sourceRow"},
      {LineNoRole, "lineNo"},
      {FunctionNameRole, "functionName"},
      {LogLevelRole, "logLevel"},
      {MarkedRole, "marked"},
      {MarkColorRole, "markColor"},
      {RawMessageRole, "rawMessage"},
      {MessageRole, "message"},
      {DateRole, "date"},
      {TagsRole, "tags"},
      {ThreadIdRole, "threadId"},
  };
}

QString LogModel::plainTextAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return {};
  }

  const auto& log_row = rows_[static_cast<size_t>(row)];
  return log_row.message.isEmpty() ? log_row.raw_message : log_row.message;
}

int LogModel::sourceRowAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return -1;
  }

  return row;
}

int LogModel::lineNoAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return 0;
  }

  return static_cast<int>(rows_[static_cast<size_t>(row)].line_no);
}

int LogModel::logLevelAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return static_cast<int>(LogLevel_Info);
  }

  return static_cast<int>(rows_[static_cast<size_t>(row)].log_level);
}

bool LogModel::markedAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return false;
  }

  return rows_[static_cast<size_t>(row)].mark_color != LineMark_None;
}

int LogModel::markColorAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return static_cast<int>(LineMark_None);
  }

  return static_cast<int>(rows_[static_cast<size_t>(row)].mark_color);
}

int LogModel::nextLineOfLevel(int row, int logLevel) const {
  const auto level = static_cast<LogLevel>(std::clamp(logLevel, static_cast<int>(LogLevel_Error),
                                                      static_cast<int>(LogLevel_Trace)));
  if (source_) {
    const auto next = source_->nextLineWithLevel(std::max(row, -1), level);
    return next ? static_cast<int>(*next) : -1;
  }

  for (int index = std::max(0, row + 1); index < rowCount(); ++index) {
    if (rows_[static_cast<size_t>(index)].log_level == level) {
      return index;
    }
  }

  return -1;
}

int LogModel::previousLineOfLevel(int row, int logLevel) const {
  const auto level = static_cast<LogLevel>(std::clamp(logLevel, static_cast<int>(LogLevel_Error),
                                                      static_cast<int>(LogLevel_Trace)));
  if (source_) {
    const auto previous = source_->previousLineWithLevel(std::max(row, 0), level);
    return previous ? static_cast<int>(*previous) : -1;
  }

  for (int index = std::min(row - 1, rowCount() - 1); index >= 0; --index) {
    if (rows_[static_cast<size_t>(index)].log_level == level) {
      return index;
    }
  }

  return -1;
}

bool LogModel::toggleMarkAt(int row, int preferredColor) {
  if (row < 0 || row >= rowCount()) {
    return false;
  }

  const auto next_color =
      markedAt(row) ? LineMark_None : normalizedMarkColor(preferredColor);
  return setMarkColorAt(row, next_color);
}

void LogModel::setFollowing(bool enabled) {
  if (following_ == enabled) {
    return;
  }

  following_ = enabled;
  if (source_) {
    source_->setFollowing(enabled);
  }
  emit followingChanged();
}

void LogModel::toggleFollowing() {
  setFollowing(!following_);
}

void LogModel::setRequestedScannerName(const QString& name) {
  if (!source_) {
    return;
  }

  const auto requested = name.trimmed();
  if (requestedScannerName() == requested) {
    return;
  }

  source_->setRequestedScannerName(requested.toStdString());
  emit scannerNameChanged();
}

LineMarkColor LogModel::normalizedMarkColor(int color) const noexcept {
  const auto clamped = std::clamp(color, static_cast<int>(LineMark_Default),
                                  static_cast<int>(LineMark_Accent6));
  return static_cast<LineMarkColor>(clamped);
}

bool LogModel::setMarkColorAt(int row, LineMarkColor color) {
  if (row < 0 || row >= rowCount()) {
    return false;
  }

  auto& log_row = rows_[static_cast<size_t>(row)];
  if (log_row.mark_color == color) {
    return false;
  }

  log_row.mark_color = color;
  const QModelIndex model_index = index(row, 0);
  emit dataChanged(model_index, model_index, {MarkedRole, MarkColorRole});
  return true;
}

LogModel::State LogModel::state() const noexcept {
  return state_;
}

const QUrl& LogModel::sourceUrl() const noexcept {
  return source_url_;
}

LogSource* LogModel::source() const noexcept {
  return source_.get();
}

bool LogModel::following() const noexcept {
  return following_;
}

bool LogModel::active() const noexcept {
  return active_;
}

QString LogModel::scannerName() const {
  if (!source_) {
    return {};
  }

  return QString::fromStdString(source_->scannerName());
}

QString LogModel::requestedScannerName() const {
  if (!source_) {
    return {};
  }

  return QString::fromStdString(source_->requestedScannerName());
}

double LogModel::linesPerSecond() const noexcept {
  return lines_per_second_;
}

qulonglong LogModel::fileSize() const noexcept {
  return file_size_;
}

void LogModel::setRows(std::vector<LogRow> rows) {
  beginResetModel();
  rows_ = std::move(rows);
  endResetModel();
  emit lineCountChanged();
}

void LogModel::appendRow(LogRow row) {
  const auto insert_at = static_cast<int>(rows_.size());
  beginInsertRows({}, insert_at, insert_at);
  rows_.push_back(std::move(row));
  endInsertRows();
  emit lineCountChanged();
}

void LogModel::markReady() {
  setState(READY);
}

void LogModel::markFailed() {
  setState(FAILED);
}

void LogModel::setSource(std::unique_ptr<LogSource> source) {
  source_ = std::move(source);
  if (!source_) {
    source_metrics_timer_.stop();
    if (lines_per_second_ != 0.0) {
      lines_per_second_ = 0.0;
      emit linesPerSecondChanged();
    }
    if (file_size_ != 0) {
      file_size_ = 0;
      emit fileSizeChanged();
    }
    setActive(false);
    setRows({});
    markFailed();
    return;
  }

  following_ = source_->snapshot().following;

  source_->setCallbacks(SourceCallbacks{
      .on_state_changed =
          [this](SourceSnapshot snapshot) {
            emit scannerNameChanged();
            if (!qFuzzyCompare(lines_per_second_ + 1.0, snapshot.lines_per_second + 1.0)) {
              lines_per_second_ = snapshot.lines_per_second;
              emit linesPerSecondChanged();
            }
            if (file_size_ != snapshot.file_size) {
              file_size_ = snapshot.file_size;
              emit fileSizeChanged();
            }
            if (snapshot.state == SourceState::Failed) {
              markFailed();
            }
            if (snapshot.state == SourceState::Ready) {
              if (reset_pending_) {
                reset_pending_ = false;
                appendRowsFromSource(0, snapshot.line_count);
              }
              markReady();
            }
          },
      .on_lines_appended =
          [this](uint64_t first_new_line, uint64_t count) {
            appendRowsFromSource(first_new_line, count);
            markActiveForRecentLines();
            refreshSourceMetrics();
          },
      .on_reset =
          [this](SourceResetReason) {
            reset_pending_ = true;
            setActive(false);
            refreshSourceMetrics();
            setRows({});
          },
      .on_error =
          [this](std::string) {
            setActive(false);
            refreshSourceMetrics();
            markFailed();
          },
  });

  source_metrics_timer_.start();
  refreshSourceMetrics();
  emit followingChanged();
  emit scannerNameChanged();
}

void LogModel::loadFromSource() {
  if (!source_) {
    markFailed();
    return;
  }

  source_->startIndexing();
  if (source_->snapshot().state == SourceState::Ready) {
    replaceRowsFromSource();
    markReady();
  } else if (source_->snapshot().state == SourceState::Failed) {
    markFailed();
  }
}

void LogModel::replaceRowsFromSource() {
  if (!source_) {
    setRows({});
    return;
  }

  std::vector<LogRow> rows;
  const auto snapshot = source_->snapshot();
  source_->fetchLines(0, static_cast<size_t>(snapshot.line_count),
                      [&rows](SourceLines lines) {
                        rows.reserve(lines.lines.size());
                        for (const auto& line : lines.lines) {
                          rows.push_back(makeRow(line));
                        }
                      });
  setRows(std::move(rows));
}

void LogModel::appendRowsFromSource(uint64_t first_line, uint64_t count) {
  if (!source_ || count == 0) {
    return;
  }

  source_->fetchLines(first_line, static_cast<size_t>(count),
                      [this](SourceLines lines) {
                        for (const auto& line : lines.lines) {
                          appendRow(makeRow(line));
                        }
                      });
}

void LogModel::markActiveForRecentLines() {
  setActive(true);
  active_timer_.start();
}

void LogModel::setActive(bool active) {
  if (active_ == active) {
    return;
  }

  active_ = active;
  emit activeChanged();
}

void LogModel::refreshSourceMetrics() {
  const auto next_lines_per_second = source_ ? source_->linesPerSecond() : 0.0;
  if (!qFuzzyCompare(lines_per_second_ + 1.0, next_lines_per_second + 1.0)) {
    lines_per_second_ = next_lines_per_second;
    emit linesPerSecondChanged();
  }

  const auto next_file_size =
      source_ ? static_cast<qulonglong>(source_->fileSize()) : qulonglong{0};
  if (file_size_ != next_file_size) {
    file_size_ = next_file_size;
    emit fileSizeChanged();
  }
}

void LogModel::setState(State state) {
  if (state_ == state) {
    return;
  }

  state_ = state;
  emit stateChanged();
}

}  // namespace lgx
