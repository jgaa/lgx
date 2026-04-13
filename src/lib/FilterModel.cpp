#include "FilterModel.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <QDateTime>
#include <QSet>
#include <QVariantMap>

namespace lgx {

namespace {

bool containsText(std::string_view haystack, std::string_view needle) {
  return needle.empty() || haystack.find(needle) != std::string_view::npos;
}

std::string_view searchTextFor(const SourceWindow& window, const SourceWindowLine& line,
                               bool raw) noexcept {
  return raw ? window.rawText(line) : window.plainText(line);
}

QString fromView(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString messageTextFromWindow(const SourceWindow& window, const SourceWindowLine& line) {
  const auto message = fromView(window.messageText(line));
  return message.isEmpty() ? fromView(window.rawText(line)) : message;
}

QString processNameFromWindow(const SourceWindow& window, const SourceWindowLine& line,
                              const QString& scanner_name) {
  if (scanner_name == QStringLiteral("Systemd")) {
    return fromView(window.functionNameText(line)).trimmed();
  }
  return {};
}

}

FilterModel::FilterModel(LogModel* source_model, QObject* parent)
    : QAbstractListModel(parent),
      source_model_(source_model) {
  refresh_timer_.setSingleShot(true);
  refresh_timer_.setInterval(75);
  connect(&refresh_timer_, &QTimer::timeout, this, &FilterModel::refresh);

  if (source_model_) {
    connect(source_model_, &QAbstractItemModel::modelReset, this, &FilterModel::onSourceRowsChanged);
    connect(source_model_, &QAbstractItemModel::rowsInserted, this, [this]() {
      onSourceRowsChanged();
    });
    connect(source_model_, &QAbstractItemModel::rowsRemoved, this, [this]() {
      onSourceRowsChanged();
    });
    connect(source_model_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& top_left, const QModelIndex& bottom_right, const QList<int>& roles) {
              onSourceDataChanged(top_left.row(), bottom_right.row(), roles);
            });
    connect(source_model_, &QObject::destroyed, this, [this]() {
      beginResetModel();
      source_model_ = nullptr;
      filtered_rows_.clear();
      endResetModel();
    });
    connect(source_model_, &LogModel::scannerNameChanged, this, &FilterModel::scannerNameChanged);
  }

  updateRegex();
  rebuildFilter();
}

FilterModel::~FilterModel() {
  prepareForRelease();
}

int FilterModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return filtered_rows_.size();
}

QVariant FilterModel::data(const QModelIndex& index, int role) const {
  if (!source_model_ || !index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  if (source_model_->source()) {
    const int source_row = filtered_rows_.at(index.row());
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(index.row(), false, &window);
    if (!line || !window) {
      return {};
    }

    switch (role) {
      case SourceRowRole:
        return source_row;
      case LineNoRole:
        return QVariant::fromValue(static_cast<qsizetype>(source_row + 1));
      case ProcessNameRole:
        return processNameFromWindow(*window, *line, scannerName());
      case FunctionNameRole:
        return fromView(window->functionNameText(*line));
      case LogLevelRole:
        return QVariant::fromValue(static_cast<int>(line->log_level));
      case MarkedRole:
        return source_model_->markedAt(source_row);
      case MarkColorRole:
        return source_model_->markColorAt(source_row);
      case RawMessageRole:
        return rawTextAt(index.row());
      case MessageRole:
        return messageTextFromWindow(*window, *line);
      case DateRole:
        return line->hasTimestamp()
            ? QVariant::fromValue(QDateTime::fromMSecsSinceEpoch(line->timestamp_msecs_since_epoch))
            : QVariant::fromValue(QDateTime{});
      case TagsRole:
        return QStringList{};
      case ThreadIdRole:
        return fromView(window->threadIdText(*line));
      case PidRole:
        return QVariant::fromValue(static_cast<qint64>(line->pid));
      case TidRole:
        return QVariant::fromValue(static_cast<qint64>(line->tid));
      default:
        return {};
    }
  }

  const int source_row = filtered_rows_.at(index.row());
  return source_model_->data(source_model_->index(source_row, 0), role);
}

QHash<int, QByteArray> FilterModel::roleNames() const {
  static const QHash<int, QByteArray> roles{
      {SourceRowRole, "sourceRow"},
      {LineNoRole, "lineNo"},
      {ProcessNameRole, "processName"},
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
  return roles;
}

QObject* FilterModel::sourceModelObject() const noexcept {
  return source_model_;
}

QUrl FilterModel::sourceUrl() const {
  return source_model_ ? source_model_->sourceUrl() : QUrl{};
}

QString FilterModel::pattern() const {
  return pattern_;
}

bool FilterModel::regex() const noexcept {
  return regex_;
}

bool FilterModel::raw() const noexcept {
  return raw_;
}

bool FilterModel::caseInsensitive() const noexcept {
  return case_insensitive_;
}

bool FilterModel::autoRefresh() const noexcept {
  return auto_refresh_;
}

bool FilterModel::dirty() const noexcept {
  return dirty_;
}

QString FilterModel::regexError() const {
  return regex_error_;
}

QString FilterModel::scannerName() const {
  return source_model_ ? source_model_->scannerName() : QString{};
}

int FilterModel::selectedPid() const noexcept {
  return selected_pid_;
}

QString FilterModel::selectedProcessName() const {
  return selected_process_name_;
}

QString FilterModel::plainTextAt(int row) const {
  if (!source_model_) {
    return {};
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, false, &window);
    return line && window ? messageTextFromWindow(*window, *line) : QString{};
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->plainTextAt(source_row) : QString{};
}

QString FilterModel::rawTextAt(int row) const {
  if (!source_model_) {
    return {};
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, true, &window);
    return line && window ? fromView(window->rawText(*line)) : QString{};
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->rawTextAt(source_row) : QString{};
}

int FilterModel::sourceRowAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return -1;
  }

  return filtered_rows_.at(row);
}

int FilterModel::proxyRowAtOrAfterSourceRow(int source_row) const {
  if (source_row < 0 || filtered_rows_.isEmpty()) {
    return -1;
  }

  const auto it = std::lower_bound(filtered_rows_.cbegin(), filtered_rows_.cend(), source_row);
  if (it != filtered_rows_.cend()) {
    return static_cast<int>(std::distance(filtered_rows_.cbegin(), it));
  }

  return filtered_rows_.size() - 1;
}

const SourceWindowLine* FilterModel::lineAtRow(int row, bool raw,
                                               const SourceWindow** window) const {
  if (window) {
    *window = nullptr;
  }
  if (!source_model_ || !source_model_->source() || row < 0 || row >= rowCount()) {
    return nullptr;
  }

  constexpr int kWindowRows = 128;
  constexpr int kLeadingMarginRows = 32;
  auto& cache = raw ? raw_window_ : parsed_window_;
  if (!cache.source_window || row < cache.logical_first || row >= cache.logical_last) {
    const int logical_first = std::max(0, row - kLeadingMarginRows);
    const int logical_last = std::min(rowCount(), logical_first + kWindowRows);
    if (logical_first >= logical_last) {
      cache = SparseWindowCache{};
      return nullptr;
    }

    cache.logical_first = logical_first;
    cache.logical_last = logical_last;
    cache.raw = raw;
    cache.source_rows = filtered_rows_.mid(logical_first, logical_last - logical_first);
    const int first_source_row = cache.source_rows.front();
    const int last_source_row = cache.source_rows.back();
    cache.source_window = source_model_->source()->windowForSourceRange(
        static_cast<uint64_t>(first_source_row),
        static_cast<size_t>(last_source_row - first_source_row + 1), raw);
  }

  if (!cache.source_window) {
    return nullptr;
  }

  const int source_row = filtered_rows_.at(row);
  if (window) {
    *window = cache.source_window.get();
  }
  return cache.source_window->lineForSourceRow(static_cast<uint64_t>(source_row));
}

void FilterModel::clearCachedWindows() const {
  parsed_window_ = SparseWindowCache{};
  raw_window_ = SparseWindowCache{};
}

int FilterModel::lineNoAt(int row) const {
  if (!source_model_) {
    return 0;
  }

  if (source_model_->source()) {
    const int source_row = sourceRowAt(row);
    return source_row >= 0 ? source_row + 1 : 0;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->lineNoAt(source_row) : 0;
}

QString FilterModel::processNameAt(int row) const {
  if (!source_model_) {
    return {};
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, false, &window);
    return line && window ? processNameFromWindow(*window, *line, scannerName()) : QString{};
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->processNameAt(source_row) : QString{};
}

int FilterModel::logLevelAt(int row) const {
  if (!source_model_) {
    return static_cast<int>(LogLevel_Info);
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, false, &window);
    return line ? static_cast<int>(line->log_level) : static_cast<int>(LogLevel_Info);
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->logLevelAt(source_row) : static_cast<int>(LogLevel_Info);
}

int FilterModel::pidAt(int row) const {
  if (!source_model_) {
    return 0;
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, false, &window);
    return line ? static_cast<int>(line->pid) : 0;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->pidAt(source_row) : 0;
}

int FilterModel::tidAt(int row) const {
  if (!source_model_) {
    return 0;
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, false, &window);
    return line ? static_cast<int>(line->tid) : 0;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->tidAt(source_row) : 0;
}

QVariantList FilterModel::systemdProcesses() const {
  QVariantList entries;
  QVariantMap all_entry;
  all_entry.insert(QStringLiteral("pid"), 0);
  all_entry.insert(QStringLiteral("name"), QString{});
  all_entry.insert(QStringLiteral("label"), tr("All processes"));
  entries.push_back(all_entry);

  if (!source_model_) {
    return entries;
  }

  QSet<QString> active_processes;
  if (auto* source = source_model_->source()) {
    const auto process_names = source->systemdProcessNames();
    for (const auto& name : process_names) {
      const auto trimmed = QString::fromStdString(name).trimmed();
      if (!trimmed.isEmpty()) {
        active_processes.insert(trimmed);
      }
    }
  } else {
    for (int row = 0; row < source_model_->rowCount(); ++row) {
      const auto name = source_model_->functionNameAt(row).trimmed();
      if (!name.isEmpty()) {
        active_processes.insert(name);
      }
    }
  }

  QStringList names;
  names.reserve(active_processes.size());
  for (const auto& name : active_processes) {
    names.push_back(name);
  }
  names.sort(Qt::CaseInsensitive);

  for (const auto& name : names) {
    QVariantMap entry;
    entry.insert(QStringLiteral("pid"), 0);
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("label"), name);
    entries.push_back(entry);
  }

  return entries;
}

bool FilterModel::markedAt(int row) const {
  if (!source_model_) {
    return false;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 && source_model_->markedAt(source_row);
}

int FilterModel::markColorAt(int row) const {
  if (!source_model_) {
    return static_cast<int>(LineMark_None);
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->markColorAt(source_row) : static_cast<int>(LineMark_None);
}

bool FilterModel::toggleMarkAt(int row, int preferredColor) {
  if (!source_model_) {
    return false;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 && source_model_->toggleMarkAt(source_row, preferredColor);
}

bool FilterModel::levelEnabled(int level) const noexcept {
  if (level < 0 || level >= static_cast<int>(enabled_levels_.size())) {
    return false;
  }

  return enabled_levels_.at(static_cast<size_t>(level));
}

void FilterModel::setLevelEnabled(int level, bool enabled) {
  if (level < 0 || level >= static_cast<int>(enabled_levels_.size())) {
    return;
  }

  auto& current = enabled_levels_[static_cast<size_t>(level)];
  if (current == enabled) {
    return;
  }

  current = enabled;
  emit levelsChanged();
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

void FilterModel::refresh() {
  refresh_timer_.stop();
  rebuildFilter();
}

void FilterModel::prepareForRelease() {
  refresh_timer_.stop();
  clearCachedWindows();
  if (source_model_) {
    disconnect(source_model_, nullptr, this, nullptr);
    source_model_ = nullptr;
  }
}

void FilterModel::setPattern(const QString& pattern) {
  if (pattern_ == pattern) {
    return;
  }

  pattern_ = pattern;
  emit patternChanged();
  updateRegex();
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

void FilterModel::setRaw(bool enabled) {
  if (raw_ == enabled) {
    return;
  }

  raw_ = enabled;
  emit rawChanged();
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

void FilterModel::setRegex(bool enabled) {
  if (regex_ == enabled) {
    return;
  }

  regex_ = enabled;
  emit regexChanged();
  updateRegex();
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

void FilterModel::setCaseInsensitive(bool enabled) {
  if (case_insensitive_ == enabled) {
    return;
  }

  case_insensitive_ = enabled;
  emit caseInsensitiveChanged();
  updateRegex();
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

void FilterModel::setAutoRefresh(bool enabled) {
  if (auto_refresh_ == enabled) {
    return;
  }

  auto_refresh_ = enabled;
  emit autoRefreshChanged();
  if (auto_refresh_ && dirty_) {
    scheduleRefresh();
  }
}

void FilterModel::setSelectedPid(int pid) {
  const int normalized = std::max(0, pid);
  if (selected_pid_ == normalized) {
    return;
  }

  selected_pid_ = normalized;
  emit selectedPidChanged();
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

void FilterModel::setSelectedProcessName(const QString& name) {
  const auto normalized = name.trimmed();
  if (selected_process_name_ == normalized) {
    return;
  }

  selected_process_name_ = normalized;
  emit selectedProcessNameChanged();
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

bool FilterModel::matchesSourceRow(int source_row) const {
  if (!source_model_ || source_row < 0 || source_row >= source_model_->rowCount()) {
    return false;
  }

  bool any_level_enabled = false;
  for (bool enabled : enabled_levels_) {
    if (enabled) {
      any_level_enabled = true;
      break;
    }
  }

  const bool has_text_filter = !pattern_.isEmpty();
  if (!any_level_enabled && !has_text_filter && selected_pid_ == 0
      && selected_process_name_.isEmpty()) {
    return true;
  }

  if (selected_pid_ > 0 && source_model_->pidAt(source_row) != selected_pid_) {
    return false;
  }

  if (!selected_process_name_.isEmpty()
      && QString::compare(source_model_->functionNameAt(source_row),
                          selected_process_name_, Qt::CaseSensitive) != 0) {
    return false;
  }

  if (any_level_enabled) {
    const int level = source_model_->logLevelAt(source_row);
    if (level < 0 || level >= static_cast<int>(enabled_levels_.size())
        || !enabled_levels_.at(static_cast<size_t>(level))) {
      return false;
    }
  }

  if (!has_text_filter) {
    return true;
  }

  const QString text = raw_
      ? source_model_->data(source_model_->index(source_row, 0), LogModel::RawMessageRole).toString()
      : source_model_->plainTextAt(source_row);
  if (!regex_) {
    return text.contains(pattern_, case_insensitive_ ? Qt::CaseInsensitive : Qt::CaseSensitive);
  }

  if (!compiled_regex_.isValid()) {
    return false;
  }

  return compiled_regex_.match(text).hasMatch();
}

int FilterModel::proxyRowForSourceRow(int source_row) const {
  const auto it = std::lower_bound(filtered_rows_.cbegin(), filtered_rows_.cend(), source_row);
  if (it == filtered_rows_.cend() || *it != source_row) {
    return -1;
  }

  return static_cast<int>(std::distance(filtered_rows_.cbegin(), it));
}

void FilterModel::scheduleRefresh() {
  refresh_timer_.start();
}

void FilterModel::markDirty() {
  if (dirty_) {
    return;
  }

  dirty_ = true;
  emit dirtyChanged();
}

void FilterModel::rebuildFilter() {
  QVector<int> next_rows;
  if (source_model_) {
    bool any_level_enabled = false;
    QVector<int> enabled_levels;
    enabled_levels.reserve(static_cast<qsizetype>(enabled_levels_.size()));
    for (int level = 0; level < static_cast<int>(enabled_levels_.size()); ++level) {
      if (enabled_levels_.at(static_cast<size_t>(level))) {
        any_level_enabled = true;
        enabled_levels.push_back(level);
      }
    }

    const bool has_text_filter = !pattern_.isEmpty();
    if (any_level_enabled && !has_text_filter && selected_pid_ == 0
        && selected_process_name_.isEmpty()) {
      QVector<int> next_source_rows;
      next_source_rows.reserve(enabled_levels.size());
      for (int level : enabled_levels) {
        next_source_rows.push_back(source_model_->nextLineOfLevel(-1, level));
      }

      while (true) {
        int next_match = -1;
        for (int candidate : next_source_rows) {
          if (candidate >= 0 && (next_match < 0 || candidate < next_match)) {
            next_match = candidate;
          }
        }

        if (next_match < 0) {
          break;
        }

        next_rows.push_back(next_match);
        for (int index = 0; index < next_source_rows.size(); ++index) {
          if (next_source_rows.at(index) == next_match) {
            next_source_rows[index] =
                source_model_->nextLineOfLevel(next_match, enabled_levels.at(index));
          }
        }
      }
    } else {
      next_rows.reserve(source_model_->rowCount());
      for (int source_row = 0; source_row < source_model_->rowCount(); ++source_row) {
        if (matchesSourceRow(source_row)) {
          next_rows.push_back(source_row);
        }
      }
    }
  }

  beginResetModel();
  filtered_rows_ = std::move(next_rows);
  clearCachedWindows();
  endResetModel();

  if (dirty_) {
    dirty_ = false;
    emit dirtyChanged();
  }
}

void FilterModel::updateRegex() {
  QString next_error;
  if (!regex_ || pattern_.isEmpty()) {
    compiled_regex_ = QRegularExpression{};
  } else {
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (case_insensitive_) {
      options |= QRegularExpression::CaseInsensitiveOption;
    }
    compiled_regex_ = QRegularExpression(pattern_, options);
    if (!compiled_regex_.isValid()) {
      next_error = compiled_regex_.errorString();
    }
  }

  if (regex_error_ != next_error) {
    regex_error_ = next_error;
    emit regexErrorChanged();
  }
}

void FilterModel::onSourceRowsChanged() {
  clearCachedWindows();
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

void FilterModel::onSourceDataChanged(int first_row, int last_row, const QList<int>& roles) {
  clearCachedWindows();
  bool affects_filter = roles.isEmpty();
  bool affects_marks = roles.isEmpty();
  for (const int role : roles) {
    if (role == LogModel::MarkedRole || role == LogModel::MarkColorRole) {
      affects_marks = true;
    }
    if (role == LogModel::LogLevelRole || role == LogModel::MessageRole
        || role == LogModel::RawMessageRole || role == LogModel::FunctionNameRole) {
      affects_filter = true;
    }
  }

  if (affects_filter) {
    if (auto_refresh_) {
      scheduleRefresh();
    } else {
      markDirty();
    }
  }

  if (!affects_marks || filtered_rows_.isEmpty()) {
    return;
  }

  for (int source_row = first_row; source_row <= last_row; ++source_row) {
    const int proxy_row = proxyRowForSourceRow(source_row);
    if (proxy_row < 0) {
      continue;
    }

    const QModelIndex proxy_index = index(proxy_row, 0);
    emit dataChanged(proxy_index, proxy_index, {MarkedRole, MarkColorRole});
  }
}

}  // namespace lgx
