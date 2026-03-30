#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <array>

#include "LogTypes.h"

namespace lgx {

class UiSettings : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList logFontFamilies READ logFontFamilies CONSTANT)
  Q_PROPERTY(QString logFontFamily READ logFontFamily WRITE setLogFontFamily NOTIFY logFontFamilyChanged)
  Q_PROPERTY(int logBaseFontPixelSize READ logBaseFontPixelSize WRITE setLogBaseFontPixelSize NOTIFY logBaseFontPixelSizeChanged)
  Q_PROPERTY(int logZoomPercent READ logZoomPercent WRITE setLogZoomPercent NOTIFY logZoomPercentChanged)
  Q_PROPERTY(int effectiveLogFontPixelSize READ effectiveLogFontPixelSize NOTIFY effectiveLogFontPixelSizeChanged)
  Q_PROPERTY(bool followLiveLogsByDefault READ followLiveLogsByDefault WRITE setFollowLiveLogsByDefault NOTIFY followLiveLogsByDefaultChanged)
  Q_PROPERTY(QVariantList logLevelStyles READ logLevelStyles NOTIFY logLevelStylesChanged)
  Q_PROPERTY(int logLevelStylesRevision READ logLevelStylesRevision NOTIFY logLevelStylesChanged)
  Q_PROPERTY(int lineMarkColorsRevision READ lineMarkColorsRevision NOTIFY lineMarkColorsChanged)
  Q_PROPERTY(int minLogBaseFontPixelSize READ minLogBaseFontPixelSize CONSTANT)
  Q_PROPERTY(int maxLogBaseFontPixelSize READ maxLogBaseFontPixelSize CONSTANT)
  Q_PROPERTY(int minLogZoomPercent READ minLogZoomPercent CONSTANT)
  Q_PROPERTY(int maxLogZoomPercent READ maxLogZoomPercent CONSTANT)

 public:
  explicit UiSettings(QObject* parent = nullptr);

  static UiSettings& instance();

  [[nodiscard]] QStringList logFontFamilies() const;
  [[nodiscard]] QString logFontFamily() const noexcept;
  [[nodiscard]] int logBaseFontPixelSize() const noexcept;
  [[nodiscard]] int logZoomPercent() const noexcept;
  [[nodiscard]] int effectiveLogFontPixelSize() const noexcept;
  [[nodiscard]] bool followLiveLogsByDefault() const noexcept;
  [[nodiscard]] QVariantList logLevelStyles() const;
  [[nodiscard]] int logLevelStylesRevision() const noexcept;
  [[nodiscard]] int lineMarkColorsRevision() const noexcept;
  [[nodiscard]] int minLogBaseFontPixelSize() const noexcept;
  [[nodiscard]] int maxLogBaseFontPixelSize() const noexcept;
  [[nodiscard]] int minLogZoomPercent() const noexcept;
  [[nodiscard]] int maxLogZoomPercent() const noexcept;

  Q_INVOKABLE void setLogFontFamily(const QString& family);
  Q_INVOKABLE void setLogBaseFontPixelSize(int pixel_size);
  Q_INVOKABLE void setLogZoomPercent(int percent);
  Q_INVOKABLE void setFollowLiveLogsByDefault(bool enabled);
  Q_INVOKABLE QString logLevelForegroundColor(int level) const;
  Q_INVOKABLE QString logLevelBackgroundColor(int level) const;
  Q_INVOKABLE QString lineMarkColor(int color) const;
  Q_INVOKABLE void setLogLevelForegroundColor(int level, const QString& color);
  Q_INVOKABLE void setLogLevelBackgroundColor(int level, const QString& color);
  Q_INVOKABLE void setLineMarkColor(int color, const QString& value);
  Q_INVOKABLE void stepLogZoom(int steps);
  Q_INVOKABLE void resetLogZoom();

signals:
  void logFontFamilyChanged();
  void logBaseFontPixelSizeChanged();
  void logZoomPercentChanged();
  void effectiveLogFontPixelSizeChanged();
  void followLiveLogsByDefaultChanged();
  void logLevelStylesChanged();
  void lineMarkColorsChanged();

 private:
  void saveValue(const QString& key, const QVariant& value) const;
  [[nodiscard]] QString defaultLogFontFamily() const;
  [[nodiscard]] int clampBaseFontPixelSize(int pixel_size) const noexcept;
  [[nodiscard]] int clampZoomPercent(int percent) const noexcept;
  [[nodiscard]] size_t colorIndexForLevel(int level) const noexcept;
  [[nodiscard]] size_t colorIndexForMark(int color) const noexcept;
  [[nodiscard]] QString colorForLevel(const std::array<QString, number_of_log_levels>& colors, int level) const;
  [[nodiscard]] QString colorForMark(const std::array<QString, number_of_line_mark_slots>& colors, int color) const;
  [[nodiscard]] QString normalizedColor(const QString& color, const QString& fallback) const;
  void setLogLevelColor(std::array<QString, number_of_log_levels>& colors, const QString& key_prefix, int level,
                        const QString& color);
  void setLineMarkSlotColor(int color, const QString& value);

  QStringList log_font_families_;
  QString log_font_family_;
  int log_base_font_pixel_size_{13};
  int log_zoom_percent_{100};
  bool follow_live_logs_by_default_{true};
  std::array<QString, number_of_log_levels> log_level_foreground_colors_{};
  std::array<QString, number_of_log_levels> log_level_background_colors_{};
  int log_level_styles_revision_{0};
  std::array<QString, number_of_line_mark_slots> line_mark_colors_{};
  int line_mark_colors_revision_{0};
};

}  // namespace lgx
