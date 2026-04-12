#include "LogModel.h"
#include "logging.h"

#include <algorithm>
#include <limits>

#include <QDateTime>

namespace lgx {

namespace {

LogRow makeRow(const SourceLine& line) {
  return LogRow{
      .line_no = static_cast<qsizetype>(line.line_number + 1U),
      .pid = line.pid,
      .tid = line.tid,
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

QString fromView(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString sourceLabel(const QUrl& source_url) {
  if (source_url.isLocalFile()) {
    return source_url.toLocalFile();
  }
  return source_url.toString();
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

LogModel::~LogModel() {
  active_timer_.stop();
  source_metrics_timer_.stop();
  current_ = false;
  active_ = false;

  if (source_) {
    source_->setCallbacks({});
    source_.reset();
  }
}

int LogModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return source_ ? row_count_cache_ : static_cast<int>(rows_.size());
}

QVariant LogModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  const auto mark_color = markColorAt(index.row());
  if (source_) {
    const auto view = lineViewAt(index.row());
    if (!view.has_value()) {
      return {};
    }

    switch (role) {
      case SourceRowRole:
        return index.row();
      case LineNoRole:
        return QVariant::fromValue(static_cast<qsizetype>(view->line_number + 1U));
      case FunctionNameRole:
        return fromView(view->functionNameText());
      case LogLevelRole:
        return QVariant::fromValue(static_cast<int>(view->log_level));
      case PidRole:
        return QVariant::fromValue(static_cast<qint64>(view->pid));
      case TidRole:
        return QVariant::fromValue(static_cast<qint64>(view->tid));
      case MarkedRole:
        return mark_color != static_cast<int>(LineMark_None);
      case MarkColorRole:
        return QVariant::fromValue(mark_color);
      case RawMessageRole:
        return fromView(view->rawText());
      case MessageRole:
        return fromView(view->plainText());
      case DateRole:
        return view->hasTimestamp()
            ? QVariant::fromValue(QDateTime::fromMSecsSinceEpoch(view->timestamp_msecs_since_epoch))
            : QVariant::fromValue(QDateTime{});
      case TagsRole:
        return QStringList{};
      case ThreadIdRole:
        return fromView(view->threadIdText());
      default:
        return {};
    }
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
    case PidRole:
      return QVariant::fromValue(row.pid);
    case TidRole:
      return QVariant::fromValue(row.tid);
    case MarkedRole:
      return mark_color != static_cast<int>(LineMark_None);
    case MarkColorRole:
      return QVariant::fromValue(mark_color);
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
      {PidRole, "pid"},
      {TidRole, "tid"},
  };
}

QString LogModel::plainTextAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return {};
  }

  if (source_) {
    const auto view = lineViewAt(row);
    return view.has_value() ? fromView(view->plainText()) : QString{};
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

  if (source_) {
    const auto view = lineViewAt(row);
    return view.has_value() ? static_cast<int>(view->line_number + 1U) : 0;
  }

  return static_cast<int>(rows_[static_cast<size_t>(row)].line_no);
}

QString LogModel::functionNameAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return {};
  }

  if (source_) {
    const auto view = lineViewAt(row);
    return view.has_value() ? fromView(view->functionNameText()) : QString{};
  }

  return rows_[static_cast<size_t>(row)].function_name;
}

int LogModel::logLevelAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return static_cast<int>(LogLevel_Info);
  }

  if (source_) {
    const auto view = lineViewAt(row);
    return view.has_value() ? static_cast<int>(view->log_level) : static_cast<int>(LogLevel_Info);
  }

  return static_cast<int>(rows_[static_cast<size_t>(row)].log_level);
}

int LogModel::pidAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return 0;
  }

  if (source_) {
    const auto view = lineViewAt(row);
    return view.has_value() ? static_cast<int>(view->pid) : 0;
  }

  return static_cast<int>(rows_[static_cast<size_t>(row)].pid);
}

int LogModel::tidAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return 0;
  }

  if (source_) {
    const auto view = lineViewAt(row);
    return view.has_value() ? static_cast<int>(view->tid) : 0;
  }

  return static_cast<int>(rows_[static_cast<size_t>(row)].tid);
}

bool LogModel::markedAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return false;
  }

  if (const auto it = marked_rows_.find(row); it != marked_rows_.end()) {
    return it->second != LineMark_None;
  }

  if (source_) {
    return false;
  }

  return rows_[static_cast<size_t>(row)].mark_color != LineMark_None;
}

int LogModel::markColorAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return static_cast<int>(LineMark_None);
  }

  if (const auto it = marked_rows_.find(row); it != marked_rows_.end()) {
    return static_cast<int>(it->second);
  }

  if (source_) {
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

void LogModel::setCurrent(bool current) {
  if (current_ == current) {
    if (current_) {
      refreshSourceMetrics();
    }
    return;
  }

  current_ = current;
  if (!current_) {
    source_metrics_timer_.stop();
    return;
  }

  source_metrics_timer_.start();
  refreshSourceMetrics();
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

  if (!source_) {
    auto& log_row = rows_[static_cast<size_t>(row)];
    if (log_row.mark_color == color) {
      return false;
    }

    log_row.mark_color = color;
  } else {
    const auto existing = marked_rows_.find(row);
    const auto current_color = existing == marked_rows_.end() ? LineMark_None : existing->second;
    if (current_color == color) {
      return false;
    }

    if (color == LineMark_None) {
      marked_rows_.erase(row);
    } else {
      marked_rows_[row] = color;
    }
  }
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

bool LogModel::catchingUp() const noexcept {
  return catching_up_;
}

bool LogModel::live() const noexcept {
  return state_ == READY && !catching_up_;
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
  marked_rows_.clear();
  row_count_cache_ = static_cast<int>(rows_.size());
  endResetModel();
  emit lineCountChanged();
}

void LogModel::appendRow(LogRow row) {
  const auto insert_at = static_cast<int>(rows_.size());
  beginInsertRows({}, insert_at, insert_at);
  rows_.push_back(std::move(row));
  row_count_cache_ = static_cast<int>(rows_.size());
  endInsertRows();
  emit lineCountChanged();
}

void LogModel::markReady() {
  setState(READY);
}

void LogModel::markFailed() {
  setCatchingUp(false);
  setState(FAILED);
}

void LogModel::setSource(std::unique_ptr<LogSource> source) {
  if (source_) {
    source_->setCallbacks({});
    source_.reset();
  }
  source_ = std::move(source);
  marked_rows_.clear();
  reset_pending_ = false;
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

  const auto initial_snapshot = source_->snapshot();
  following_ = initial_snapshot.following;
  row_count_cache_ = static_cast<int>(initial_snapshot.line_count);
  beginCatchUp();
  updateSourceStatus(initial_snapshot);

  source_->setCallbacks(SourceCallbacks{
      .on_state_changed =
          [this](SourceSnapshot snapshot) {
            emit scannerNameChanged();
            if (reset_pending_ && snapshot.state == SourceState::Ready) {
              reset_pending_ = false;
              syncSourceRowCount(snapshot.line_count, false);
            } else if (!reset_pending_ && row_count_cache_ > static_cast<int>(snapshot.line_count)) {
              syncSourceRowCount(snapshot.line_count, true);
            }
            if (current_
                && !qFuzzyCompare(lines_per_second_ + 1.0, snapshot.lines_per_second + 1.0)) {
              lines_per_second_ = snapshot.lines_per_second;
              emit linesPerSecondChanged();
            }
            if (current_ && file_size_ != snapshot.file_size) {
              file_size_ = snapshot.file_size;
              emit fileSizeChanged();
            }
            updateSourceStatus(snapshot);
          },
      .on_lines_appended =
          [this](uint64_t first_new_line, uint64_t count) {
            if (!reset_pending_) {
              syncSourceRowCount(first_new_line + count);
            }
            markActiveForRecentLines();
            if (current_) {
              refreshSourceMetrics();
            }
          },
      .on_reset =
          [this](SourceResetReason) {
            setActive(false);
            marked_rows_.clear();
            reset_pending_ = true;
            beginCatchUp();
            if (row_count_cache_ != 0) {
              syncSourceRowCount(0, true);
            }
            refreshSourceMetrics();
          },
      .on_error =
          [this](std::string) {
            setActive(false);
            if (current_) {
              refreshSourceMetrics();
            }
            setCatchingUp(false);
            markFailed();
          },
  });

  if (current_) {
    source_metrics_timer_.start();
  }
  refreshSourceMetrics();
  emit lineCountChanged();
  emit followingChanged();
  emit scannerNameChanged();
}

void LogModel::loadFromSource() {
  if (!source_) {
    markFailed();
    return;
  }

  beginCatchUp();
  source_->startIndexing();
  const auto snapshot = source_->snapshot();
  syncSourceRowCount(snapshot.line_count, true);
  updateSourceStatus(snapshot);
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

void LogModel::syncSourceRowCount(uint64_t next_row_count, bool force_reset) {
  const int normalized = static_cast<int>(std::min<uint64_t>(
      next_row_count, static_cast<uint64_t>(std::numeric_limits<int>::max())));
  if (force_reset) {
    beginResetModel();
    row_count_cache_ = normalized;
    endResetModel();
    emit lineCountChanged();
    return;
  }

  if (row_count_cache_ == normalized) {
    return;
  }

  if (normalized < row_count_cache_) {
    beginResetModel();
    row_count_cache_ = normalized;
    endResetModel();
  } else {
    beginInsertRows({}, row_count_cache_, normalized - 1);
    row_count_cache_ = normalized;
    endInsertRows();
  }
  emit lineCountChanged();
}

std::optional<SourceLineView> LogModel::lineViewAt(int row) const {
  if (!source_ || row < 0 || row >= rowCount()) {
    return std::nullopt;
  }

  return source_->lineViewAt(static_cast<uint64_t>(row));
}

void LogModel::beginCatchUp() {
  catch_up_started_at_ = std::chrono::steady_clock::now();
  setCatchingUp(true);
}

void LogModel::logSourceReady(uint64_t line_count) {
  if (!catch_up_started_at_.has_value()) {
    return;
  }

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - *catch_up_started_at_).count();
  catch_up_started_at_.reset();
  LOG_INFO << "Source ready: '" << sourceLabel(source_url_).toStdString()
           << "', lines=" << line_count
           << ", catchUpMs=" << elapsed_ms;
}

void LogModel::updateSourceStatus(const SourceSnapshot& snapshot) {
  const auto was_live = live();

  setCatchingUp(snapshot.catching_up);

  if (snapshot.state == SourceState::Failed) {
    markFailed();
    return;
  }

  if (snapshot.state == SourceState::Ready) {
    markReady();
    if (!was_live && live()) {
      logSourceReady(snapshot.line_count);
    }
    return;
  }

  setState(INITIALIZING);
}

void LogModel::setCatchingUp(bool catching_up) {
  if (catching_up_ == catching_up) {
    return;
  }

  catching_up_ = catching_up;
  emit sourceStatusChanged();
}

void LogModel::setState(State state) {
  if (state_ == state) {
    return;
  }

  const auto old_live = live();
  state_ = state;
  emit stateChanged();
  if (old_live != live()) {
    emit sourceStatusChanged();
  }
}

}  // namespace lgx
