#include "UiSettings.h"

#include <QColor>
#include <QFontDatabase>
#include <QSettings>

#include <algorithm>
#include <array>

#include "LogScanner.h"

namespace lgx {
namespace {

constexpr auto kLogFontFamilyKey = "ui/log/fontFamily";
constexpr auto kLogBaseFontPixelSizeKey = "ui/log/baseFontPixelSize";
constexpr auto kLogZoomPercentKey = "ui/log/zoomPercent";
constexpr auto kFollowLiveLogsByDefaultKey = "ui/log/followLiveLogsByDefault";
constexpr auto kWrapLogLinesByDefaultKey = "ui/log/wrapLogLinesByDefault";
constexpr auto kFollowScrollIntervalMsKey = "ui/log/followScrollIntervalMs";
constexpr auto kFollowHighRateScrollIntervalMsKey = "ui/log/followHighRateScrollIntervalMs";
constexpr auto kDefaultLogScannerNameKey = "ui/log/defaultScannerName";
constexpr auto kAdbExecutablePathKey = "ui/adb/executablePath";
constexpr int kDefaultLogBaseFontPixelSize = 13;
constexpr int kMinLogBaseFontPixelSize = 8;
constexpr int kMaxLogBaseFontPixelSize = 36;
constexpr int kDefaultLogZoomPercent = 100;
constexpr int kMinLogZoomPercent = 50;
constexpr int kMaxLogZoomPercent = 400;
constexpr int kLogZoomStepPercent = 10;
constexpr int kDefaultFollowScrollIntervalMs = 300;
constexpr int kDefaultFollowHighRateScrollIntervalMs = 5000;
constexpr int kMinFollowScrollIntervalMs = 50;
constexpr int kMaxFollowScrollIntervalMs = 60'000;

struct LineMarkStyleDefinition {
  LineMarkColor color;
  const char* key;
  const char* fallback;
};

struct LogLevelStyleDefinition {
  LogLevel level;
  const char* key;
  const char* name;
  const char* foreground;
  const char* background;
};

constexpr std::array kLogLevelStyleDefinitions{
    LogLevelStyleDefinition{LogLevel_Error, "error", "Error", "red", "yellow"},
    LogLevelStyleDefinition{LogLevel_Warn, "warning", "Warning", "orangered", "white"},
    LogLevelStyleDefinition{LogLevel_Notice, "notice", "Notice", "blue", "white"},
    LogLevelStyleDefinition{LogLevel_Info, "info", "Info", "navy", "white"},
    LogLevelStyleDefinition{LogLevel_Debug, "debug", "Debug", "teal", "white"},
    LogLevelStyleDefinition{LogLevel_Trace, "trace", "Trace", "gray", "white"},
};

constexpr std::array kLineMarkStyleDefinitions{
    LineMarkStyleDefinition{LineMark_Default, "default", "#2e7d32"},
    LineMarkStyleDefinition{LineMark_Accent1, "accent1", "#c62828"},
    LineMarkStyleDefinition{LineMark_Accent2, "accent2", "#ef6c00"},
    LineMarkStyleDefinition{LineMark_Accent3, "accent3", "#f9a825"},
    LineMarkStyleDefinition{LineMark_Accent4, "accent4", "#1565c0"},
    LineMarkStyleDefinition{LineMark_Accent5, "accent5", "#00838f"},
    LineMarkStyleDefinition{LineMark_Accent6, "accent6", "#111111"},
};

const LogLevelStyleDefinition& definitionForLevel(int level) {
  const auto clamped = static_cast<LogLevel>(
      std::clamp(level, static_cast<int>(LogLevel_Error), static_cast<int>(LogLevel_Trace)));
  return kLogLevelStyleDefinitions.at(static_cast<size_t>(clamped));
}

QString colorKey(const char* level_key, const char* channel) {
  return QStringLiteral("ui/log/colors/%1/%2")
      .arg(QString::fromUtf8(level_key), QString::fromUtf8(channel));
}

QString markColorKey(const char* slot_key) {
  return QStringLiteral("ui/log/marks/%1").arg(QString::fromUtf8(slot_key));
}

QString migrateLegacyNamedColorChoice(const QString& color) {
  const auto normalized = color.trimmed().toLower();
  if (normalized == "#fffaf0") {
    return QStringLiteral("#fffff0");
  }
  if (normalized == "#fff3a6") {
    return QStringLiteral("#ffffe0");
  }
  if (normalized == "#facc15") {
    return QStringLiteral("#ffff00");
  }
  if (normalized == "#f59e0b" || normalized == "#d97706" || normalized == "#ffa500") {
    return QStringLiteral("orangered");
  }
  if (normalized == "#b22222") {
    return QStringLiteral("#ff0000");
  }
  if (normalized == "#7f1d1d") {
    return QStringLiteral("#800020");
  }
  if (normalized == "#8b5e3c") {
    return QStringLiteral("#a52a2a");
  }
  if (normalized == "#dcfce7") {
    return QStringLiteral("#f5fffa");
  }
  if (normalized == "#15803d") {
    return QStringLiteral("#008000");
  }
  if (normalized == "#0f766e") {
    return QStringLiteral("#008080");
  }
  if (normalized == "#0891b2") {
    return QStringLiteral("#00ffff");
  }
  if (normalized == "#0ea5e9") {
    return QStringLiteral("#87ceeb");
  }
  if (normalized == "#1e3a5f") {
    return QStringLiteral("#000080");
  }
  if (normalized == "#4338ca") {
    return QStringLiteral("#4b0082");
  }
  if (normalized == "#7c3aed") {
    return QStringLiteral("#ee82ee");
  }
  if (normalized == "#db2777") {
    return QStringLiteral("#ffc0cb");
  }
  if (normalized == "#d1d5db") {
    return QStringLiteral("#c0c0c0");
  }
  if (normalized == "#6b7280") {
    return QStringLiteral("#808080");
  }
  if (normalized == "#475569") {
    return QStringLiteral("#708090");
  }
  if (normalized == "#2c2823") {
    return QStringLiteral("#36454f");
  }
  if (normalized == "#111111") {
    return QStringLiteral("#000000");
  }

  return color;
}

}  // namespace

UiSettings::UiSettings(QObject* parent)
    : QObject(parent) {
  QFontDatabase font_database;
  log_font_families_ = font_database.families();
  std::sort(log_font_families_.begin(), log_font_families_.end());
  log_font_families_.removeDuplicates();
  available_log_scanner_names_ = lgx::availableLogScannerNames();
  available_log_scanner_names_.removeAll(QStringLiteral("Auto"));

  const QSettings settings;
  const auto default_family = defaultLogFontFamily();
  const auto stored_family = settings.value(QLatin1StringView{kLogFontFamilyKey}, default_family).toString().trimmed();
  log_font_family_ = stored_family.isEmpty() ? default_family : stored_family;
  log_base_font_pixel_size_ = clampBaseFontPixelSize(
      settings.value(QLatin1StringView{kLogBaseFontPixelSizeKey}, kDefaultLogBaseFontPixelSize).toInt());
  log_zoom_percent_ = clampZoomPercent(
      settings.value(QLatin1StringView{kLogZoomPercentKey}, kDefaultLogZoomPercent).toInt());
  follow_live_logs_by_default_ =
      settings.value(QLatin1StringView{kFollowLiveLogsByDefaultKey}, true).toBool();
  wrap_log_lines_by_default_ =
      settings.value(QLatin1StringView{kWrapLogLinesByDefaultKey}, false).toBool();
  follow_scroll_interval_ms_ = clampFollowScrollIntervalMs(
      settings.value(QLatin1StringView{kFollowScrollIntervalMsKey}, kDefaultFollowScrollIntervalMs).toInt());
  follow_high_rate_scroll_interval_ms_ = clampFollowScrollIntervalMs(
      settings.value(QLatin1StringView{kFollowHighRateScrollIntervalMsKey}, kDefaultFollowHighRateScrollIntervalMs)
          .toInt());
  default_log_scanner_name_ = normalizeDefaultLogScannerName(
      settings.value(QLatin1StringView{kDefaultLogScannerNameKey}, QStringLiteral("Generic")).toString());
  adb_executable_path_ =
      settings.value(QLatin1StringView{kAdbExecutablePathKey}, QString{}).toString().trimmed();

  for (const auto& definition : kLogLevelStyleDefinitions) {
    const auto level_index = static_cast<size_t>(definition.level);
    log_level_foreground_colors_[level_index] = normalizedColor(
        migrateLegacyNamedColorChoice(
            settings.value(colorKey(definition.key, "foreground"), QString::fromUtf8(definition.foreground)).toString()),
        QString::fromUtf8(definition.foreground));
    log_level_background_colors_[level_index] = normalizedColor(
        migrateLegacyNamedColorChoice(
            settings.value(colorKey(definition.key, "background"), QString::fromUtf8(definition.background)).toString()),
        QString::fromUtf8(definition.background));
  }

  for (const auto& definition : kLineMarkStyleDefinitions) {
    const auto color_index = static_cast<size_t>(definition.color - 1);
    line_mark_colors_[color_index] = normalizedColor(
        settings.value(markColorKey(definition.key), QString::fromUtf8(definition.fallback)).toString(),
        QString::fromUtf8(definition.fallback));
  }
}

UiSettings& UiSettings::instance() {
  static UiSettings instance;
  return instance;
}

QStringList UiSettings::logFontFamilies() const {
  return log_font_families_;
}

QString UiSettings::logFontFamily() const noexcept {
  return log_font_family_;
}

int UiSettings::logBaseFontPixelSize() const noexcept {
  return log_base_font_pixel_size_;
}

int UiSettings::logZoomPercent() const noexcept {
  return log_zoom_percent_;
}

int UiSettings::effectiveLogFontPixelSize() const noexcept {
  return std::max(
      1,
      (log_base_font_pixel_size_ * log_zoom_percent_ + (kDefaultLogZoomPercent / 2)) / kDefaultLogZoomPercent);
}

bool UiSettings::followLiveLogsByDefault() const noexcept {
  return follow_live_logs_by_default_;
}

bool UiSettings::wrapLogLinesByDefault() const noexcept {
  return wrap_log_lines_by_default_;
}

int UiSettings::followScrollIntervalMs() const noexcept {
  return follow_scroll_interval_ms_;
}

int UiSettings::followHighRateScrollIntervalMs() const noexcept {
  return follow_high_rate_scroll_interval_ms_;
}

QStringList UiSettings::availableLogScannerNames() const {
  return available_log_scanner_names_;
}

QString UiSettings::defaultLogScannerName() const noexcept {
  return default_log_scanner_name_;
}

QString UiSettings::adbExecutablePath() const noexcept {
  return adb_executable_path_;
}

QVariantList UiSettings::logLevelStyles() const {
  QVariantList styles;
  styles.reserve(static_cast<qsizetype>(kLogLevelStyleDefinitions.size()));
  for (const auto& definition : kLogLevelStyleDefinitions) {
    QVariantMap style;
    style.insert(QStringLiteral("level"), static_cast<int>(definition.level));
    style.insert(QStringLiteral("name"), tr(definition.name));
    style.insert(QStringLiteral("foregroundColor"),
                 log_level_foreground_colors_.at(static_cast<size_t>(definition.level)));
    style.insert(QStringLiteral("backgroundColor"),
                 log_level_background_colors_.at(static_cast<size_t>(definition.level)));
    styles.push_back(style);
  }
  return styles;
}

int UiSettings::logLevelStylesRevision() const noexcept {
  return log_level_styles_revision_;
}

int UiSettings::lineMarkColorsRevision() const noexcept {
  return line_mark_colors_revision_;
}

int UiSettings::minLogBaseFontPixelSize() const noexcept {
  return kMinLogBaseFontPixelSize;
}

int UiSettings::maxLogBaseFontPixelSize() const noexcept {
  return kMaxLogBaseFontPixelSize;
}

int UiSettings::minLogZoomPercent() const noexcept {
  return kMinLogZoomPercent;
}

int UiSettings::maxLogZoomPercent() const noexcept {
  return kMaxLogZoomPercent;
}

void UiSettings::setLogFontFamily(const QString& family) {
  const auto trimmed = family.trimmed();
  const auto next_family = trimmed.isEmpty() ? defaultLogFontFamily() : trimmed;
  if (log_font_family_ == next_family) {
    return;
  }

  log_font_family_ = next_family;
  saveValue(QLatin1StringView{kLogFontFamilyKey}, log_font_family_);
  emit logFontFamilyChanged();
}

void UiSettings::setLogBaseFontPixelSize(int pixel_size) {
  const int clamped = clampBaseFontPixelSize(pixel_size);
  if (log_base_font_pixel_size_ == clamped) {
    return;
  }

  log_base_font_pixel_size_ = clamped;
  saveValue(QLatin1StringView{kLogBaseFontPixelSizeKey}, log_base_font_pixel_size_);
  emit logBaseFontPixelSizeChanged();
  emit effectiveLogFontPixelSizeChanged();
}

void UiSettings::setLogZoomPercent(int percent) {
  const int clamped = clampZoomPercent(percent);
  if (log_zoom_percent_ == clamped) {
    return;
  }

  log_zoom_percent_ = clamped;
  saveValue(QLatin1StringView{kLogZoomPercentKey}, log_zoom_percent_);
  emit logZoomPercentChanged();
  emit effectiveLogFontPixelSizeChanged();
}

void UiSettings::setFollowLiveLogsByDefault(bool enabled) {
  if (follow_live_logs_by_default_ == enabled) {
    return;
  }

  follow_live_logs_by_default_ = enabled;
  saveValue(QLatin1StringView{kFollowLiveLogsByDefaultKey}, follow_live_logs_by_default_);
  emit followLiveLogsByDefaultChanged();
}

void UiSettings::setWrapLogLinesByDefault(bool enabled) {
  if (wrap_log_lines_by_default_ == enabled) {
    return;
  }

  wrap_log_lines_by_default_ = enabled;
  saveValue(QLatin1StringView{kWrapLogLinesByDefaultKey}, enabled);
  emit wrapLogLinesByDefaultChanged();
}

void UiSettings::setFollowScrollIntervalMs(int interval_ms) {
  const int clamped = clampFollowScrollIntervalMs(interval_ms);
  if (follow_scroll_interval_ms_ == clamped) {
    return;
  }

  follow_scroll_interval_ms_ = clamped;
  saveValue(QLatin1StringView{kFollowScrollIntervalMsKey}, follow_scroll_interval_ms_);
  emit followScrollIntervalMsChanged();
}

void UiSettings::setFollowHighRateScrollIntervalMs(int interval_ms) {
  const int clamped = clampFollowScrollIntervalMs(interval_ms);
  if (follow_high_rate_scroll_interval_ms_ == clamped) {
    return;
  }

  follow_high_rate_scroll_interval_ms_ = clamped;
  saveValue(QLatin1StringView{kFollowHighRateScrollIntervalMsKey}, follow_high_rate_scroll_interval_ms_);
  emit followHighRateScrollIntervalMsChanged();
}

void UiSettings::setDefaultLogScannerName(const QString& name) {
  const auto normalized = normalizeDefaultLogScannerName(name);
  if (default_log_scanner_name_ == normalized) {
    return;
  }

  default_log_scanner_name_ = normalized;
  saveValue(QLatin1StringView{kDefaultLogScannerNameKey}, default_log_scanner_name_);
  emit defaultLogScannerNameChanged();
}

void UiSettings::setAdbExecutablePath(const QString& path) {
  const auto trimmed = path.trimmed();
  if (adb_executable_path_ == trimmed) {
    return;
  }

  adb_executable_path_ = trimmed;
  saveValue(QLatin1StringView{kAdbExecutablePathKey}, adb_executable_path_);
  emit adbExecutablePathChanged();
}

QString UiSettings::logLevelForegroundColor(int level) const {
  return colorForLevel(log_level_foreground_colors_, level);
}

QString UiSettings::logLevelBackgroundColor(int level) const {
  return colorForLevel(log_level_background_colors_, level);
}

QString UiSettings::lineMarkColor(int color) const {
  return colorForMark(line_mark_colors_, color);
}

void UiSettings::setLogLevelForegroundColor(int level, const QString& color) {
  setLogLevelColor(log_level_foreground_colors_, QStringLiteral("foreground"), level, color);
}

void UiSettings::setLogLevelBackgroundColor(int level, const QString& color) {
  setLogLevelColor(log_level_background_colors_, QStringLiteral("background"), level, color);
}

void UiSettings::setLineMarkColor(int color, const QString& value) {
  setLineMarkSlotColor(color, value);
}

void UiSettings::stepLogZoom(int steps) {
  if (steps == 0) {
    return;
  }

  setLogZoomPercent(log_zoom_percent_ + (steps * kLogZoomStepPercent));
}

void UiSettings::resetLogZoom() {
  setLogZoomPercent(kDefaultLogZoomPercent);
}

void UiSettings::saveValue(const QString& key, const QVariant& value) const {
  QSettings settings;
  settings.setValue(key, value);
  settings.sync();
}

QString UiSettings::defaultLogFontFamily() const {
  return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

int UiSettings::clampBaseFontPixelSize(int pixel_size) const noexcept {
  return std::clamp(pixel_size, kMinLogBaseFontPixelSize, kMaxLogBaseFontPixelSize);
}

int UiSettings::clampZoomPercent(int percent) const noexcept {
  return std::clamp(percent, kMinLogZoomPercent, kMaxLogZoomPercent);
}

int UiSettings::clampFollowScrollIntervalMs(int interval_ms) const noexcept {
  return std::clamp(interval_ms, kMinFollowScrollIntervalMs, kMaxFollowScrollIntervalMs);
}

QString UiSettings::normalizeDefaultLogScannerName(const QString& name) const {
  const auto trimmed = name.trimmed();
  if (available_log_scanner_names_.contains(trimmed)) {
    return trimmed;
  }

  return QStringLiteral("Generic");
}

size_t UiSettings::colorIndexForLevel(int level) const noexcept {
  return static_cast<size_t>(
      std::clamp(level, static_cast<int>(LogLevel_Error), static_cast<int>(LogLevel_Trace)));
}

size_t UiSettings::colorIndexForMark(int color) const noexcept {
  const auto clamped = std::clamp(color, static_cast<int>(LineMark_Default),
                                  static_cast<int>(LineMark_Accent6));
  return static_cast<size_t>(clamped - 1);
}

QString UiSettings::colorForLevel(const std::array<QString, number_of_log_levels>& colors, int level) const {
  return colors.at(colorIndexForLevel(level));
}

QString UiSettings::colorForMark(const std::array<QString, number_of_line_mark_slots>& colors, int color) const {
  return colors.at(colorIndexForMark(color));
}

QString UiSettings::normalizedColor(const QString& color, const QString& fallback) const {
  const QColor parsed(color.trimmed());
  if (parsed.isValid()) {
    return parsed.name(QColor::HexRgb);
  }

  const QColor fallback_color(fallback);
  return fallback_color.isValid() ? fallback_color.name(QColor::HexRgb) : QStringLiteral("#ffffff");
}

void UiSettings::setLogLevelColor(std::array<QString, number_of_log_levels>& colors, const QString& key_prefix,
                                  int level, const QString& color) {
  const auto& definition = definitionForLevel(level);
  const auto color_index = colorIndexForLevel(level);
  const bool foreground = key_prefix == QStringLiteral("foreground");
  const auto fallback =
      foreground ? QString::fromUtf8(definition.foreground) : QString::fromUtf8(definition.background);
  const auto normalized = normalizedColor(color, fallback);
  if (colors[color_index] == normalized) {
    return;
  }

  colors[color_index] = normalized;
  saveValue(colorKey(definition.key, foreground ? "foreground" : "background"), normalized);
  ++log_level_styles_revision_;
  emit logLevelStylesChanged();
}

void UiSettings::setLineMarkSlotColor(int color, const QString& value) {
  const auto definition_index = colorIndexForMark(color);
  const auto& definition = kLineMarkStyleDefinitions.at(definition_index);
  const auto normalized = normalizedColor(value, QString::fromUtf8(definition.fallback));
  if (line_mark_colors_[definition_index] == normalized) {
    return;
  }

  line_mark_colors_[definition_index] = normalized;
  saveValue(markColorKey(definition.key), normalized);
  ++line_mark_colors_revision_;
  emit lineMarkColorsChanged();
}

}  // namespace lgx
