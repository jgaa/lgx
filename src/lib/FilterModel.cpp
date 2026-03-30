#include "FilterModel.h"

#include <algorithm>

namespace lgx {

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
  }

  updateRegex();
  rebuildFilter();
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

  const int source_row = filtered_rows_.at(index.row());
  return source_model_->data(source_model_->index(source_row, 0), role);
}

QHash<int, QByteArray> FilterModel::roleNames() const {
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

QString FilterModel::plainTextAt(int row) const {
  if (!source_model_) {
    return {};
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->plainTextAt(source_row) : QString{};
}

int FilterModel::sourceRowAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return -1;
  }

  return filtered_rows_.at(row);
}

int FilterModel::lineNoAt(int row) const {
  if (!source_model_) {
    return 0;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->lineNoAt(source_row) : 0;
}

int FilterModel::logLevelAt(int row) const {
  if (!source_model_) {
    return static_cast<int>(LogLevel_Info);
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->logLevelAt(source_row) : static_cast<int>(LogLevel_Info);
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
  if (!any_level_enabled && !has_text_filter) {
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

  const QString text = source_model_->plainTextAt(source_row);
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
    if (any_level_enabled && !has_text_filter) {
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
  if (auto_refresh_) {
    scheduleRefresh();
  } else {
    markDirty();
  }
}

void FilterModel::onSourceDataChanged(int first_row, int last_row, const QList<int>& roles) {
  bool affects_filter = roles.isEmpty();
  bool affects_marks = roles.isEmpty();
  for (const int role : roles) {
    if (role == LogModel::MarkedRole || role == LogModel::MarkColorRole) {
      affects_marks = true;
    }
    if (role == LogModel::LogLevelRole || role == LogModel::MessageRole || role == LogModel::RawMessageRole) {
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
