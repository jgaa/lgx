#include "MarkedModel.h"

#include <algorithm>

#include <QDateTime>

namespace lgx {

namespace {

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

}  // namespace

MarkedModel::MarkedModel(LogModel* source_model, QObject* parent)
    : QAbstractListModel(parent),
      source_model_(source_model) {
  if (source_model_) {
    connect(source_model_, &QAbstractItemModel::modelReset, this, &MarkedModel::onSourceRowsChanged);
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
      marked_rows_.clear();
      clearCachedWindow();
      endResetModel();
    });
  }

  rebuildMarkedRows();
}

int MarkedModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return marked_rows_.size();
}

QVariant MarkedModel::data(const QModelIndex& index, int role) const {
  if (!source_model_ || !index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }

  if (source_model_->source()) {
    const int source_row = marked_rows_.at(index.row());
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(index.row(), &window);
    if (!line || !window) {
      return {};
    }

    switch (role) {
      case SourceRowRole:
        return source_row;
      case LineNoRole:
        return QVariant::fromValue(static_cast<qsizetype>(source_row + 1));
      case ProcessNameRole:
        return processNameFromWindow(*window, *line, source_model_->scannerName());
      case FunctionNameRole:
        return fromView(window->functionNameText(*line));
      case LogLevelRole:
        return QVariant::fromValue(static_cast<int>(line->log_level));
      case MarkedRole:
        return true;
      case MarkColorRole:
        return source_model_->markColorAt(source_row);
      case RawMessageRole:
        return fromView(window->rawText(*line));
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

  const int source_row = marked_rows_.at(index.row());
  return source_model_->data(source_model_->index(source_row, 0), role);
}

QHash<int, QByteArray> MarkedModel::roleNames() const {
  return {
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
}

QObject* MarkedModel::sourceModelObject() const noexcept {
  return source_model_;
}

QUrl MarkedModel::sourceUrl() const {
  return source_model_ ? source_model_->sourceUrl() : QUrl{};
}

QString MarkedModel::plainTextAt(int row) const {
  if (!source_model_) {
    return {};
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, &window);
    return line && window ? messageTextFromWindow(*window, *line) : QString{};
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->plainTextAt(source_row) : QString{};
}

QString MarkedModel::rawTextAt(int row) const {
  if (!source_model_) {
    return {};
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, &window);
    return line && window ? fromView(window->rawText(*line)) : QString{};
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->rawTextAt(source_row) : QString{};
}

int MarkedModel::sourceRowAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return -1;
  }

  return marked_rows_.at(row);
}

int MarkedModel::lineNoAt(int row) const {
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

QString MarkedModel::processNameAt(int row) const {
  if (!source_model_) {
    return {};
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, &window);
    return line && window ? processNameFromWindow(*window, *line, source_model_->scannerName())
                          : QString{};
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->processNameAt(source_row) : QString{};
}

int MarkedModel::logLevelAt(int row) const {
  if (!source_model_) {
    return static_cast<int>(LogLevel_Info);
  }

  if (source_model_->source()) {
    const SourceWindow* window = nullptr;
    const auto* line = lineAtRow(row, &window);
    return line ? static_cast<int>(line->log_level) : static_cast<int>(LogLevel_Info);
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->logLevelAt(source_row) : static_cast<int>(LogLevel_Info);
}

bool MarkedModel::markedAt(int row) const {
  if (!source_model_) {
    return false;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 && source_model_->markedAt(source_row);
}

int MarkedModel::markColorAt(int row) const {
  if (!source_model_) {
    return static_cast<int>(LineMark_None);
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 ? source_model_->markColorAt(source_row) : static_cast<int>(LineMark_None);
}

bool MarkedModel::toggleMarkAt(int row, int preferredColor) {
  if (!source_model_) {
    return false;
  }

  const int source_row = sourceRowAt(row);
  return source_row >= 0 && source_model_->toggleMarkAt(source_row, preferredColor);
}

const SourceWindowLine* MarkedModel::lineAtRow(int row, const SourceWindow** window) const {
  if (window) {
    *window = nullptr;
  }
  if (!source_model_ || !source_model_->source() || row < 0 || row >= rowCount()) {
    return nullptr;
  }

  constexpr int kWindowRows = 128;
  constexpr int kLeadingMarginRows = 32;
  if (!window_cache_.source_window || row < window_cache_.logical_first || row >= window_cache_.logical_last) {
    const int logical_first = std::max(0, row - kLeadingMarginRows);
    const int logical_last = std::min(rowCount(), logical_first + kWindowRows);
    if (logical_first >= logical_last) {
      window_cache_ = SparseWindowCache{};
      return nullptr;
    }

    window_cache_.logical_first = logical_first;
    window_cache_.logical_last = logical_last;
    window_cache_.source_rows = marked_rows_.mid(logical_first, logical_last - logical_first);
    const int first_source_row = window_cache_.source_rows.front();
    const int last_source_row = window_cache_.source_rows.back();
    window_cache_.source_window = source_model_->source()->windowForSourceRange(
        static_cast<uint64_t>(first_source_row),
        static_cast<size_t>(last_source_row - first_source_row + 1), false);
  }

  if (!window_cache_.source_window) {
    return nullptr;
  }

  const int source_row = marked_rows_.at(row);
  if (window) {
    *window = window_cache_.source_window.get();
  }
  return window_cache_.source_window->lineForSourceRow(static_cast<uint64_t>(source_row));
}

void MarkedModel::clearCachedWindow() const {
  window_cache_ = SparseWindowCache{};
}

int MarkedModel::proxyRowForSourceRow(int source_row) const {
  const auto it = std::lower_bound(marked_rows_.cbegin(), marked_rows_.cend(), source_row);
  if (it == marked_rows_.cend() || *it != source_row) {
    return -1;
  }

  return static_cast<int>(std::distance(marked_rows_.cbegin(), it));
}

void MarkedModel::rebuildMarkedRows() {
  QVector<int> next_rows;
  if (source_model_) {
    next_rows.reserve(source_model_->rowCount());
    for (int source_row = 0; source_row < source_model_->rowCount(); ++source_row) {
      if (source_model_->markedAt(source_row)) {
        next_rows.push_back(source_row);
      }
    }
  }

  beginResetModel();
  marked_rows_ = std::move(next_rows);
  clearCachedWindow();
  endResetModel();
}

void MarkedModel::onSourceRowsChanged() {
  clearCachedWindow();
  rebuildMarkedRows();
}

void MarkedModel::onSourceDataChanged(int first_row, int last_row, const QList<int>& roles) {
  clearCachedWindow();
  bool affects_marks = roles.isEmpty();
  bool affects_other_data = roles.isEmpty();
  for (const int role : roles) {
    if (role == LogModel::MarkedRole || role == LogModel::MarkColorRole) {
      affects_marks = true;
    } else {
      affects_other_data = true;
    }
  }

  if (affects_marks) {
    rebuildMarkedRows();
  }

  if (!affects_other_data || marked_rows_.isEmpty()) {
    return;
  }

  for (int source_row = first_row; source_row <= last_row; ++source_row) {
    const int proxy_row = proxyRowForSourceRow(source_row);
    if (proxy_row < 0) {
      continue;
    }

    const QModelIndex proxy_index = index(proxy_row, 0);
    emit dataChanged(proxy_index, proxy_index, roles);
  }
}

}  // namespace lgx
