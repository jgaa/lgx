#include <QApplication>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPointer>
#include <QQmlError>
#include <QScreen>
#include <QSettings>
#include <QVariant>
#include <QWindow>

#include <optional>

#include "AppEngine.h"
#include "UiSettings.h"
#include "logging.h"
#include "util.h"

namespace {

constexpr auto kWindowWidthKey = "ui/window/width";
constexpr auto kWindowHeightKey = "ui/window/height";
constexpr auto kMaterialSymbolsFontResource = ":/qt/qml/lgx/qml/fonts/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf";

struct SavedWindowSize {
  QSize size;
};

void registerApplicationFonts() {
  const int material_symbols_font_id = QFontDatabase::addApplicationFont(QString::fromUtf8(kMaterialSymbolsFontResource));
  if (material_symbols_font_id < 0) {
    LOG_WARN << "Failed to load font resource " << kMaterialSymbolsFontResource;
  }
}

bool isSavedWindowSizeValid(const SavedWindowSize& saved_size) {
  if (!saved_size.size.isValid()) {
    return false;
  }

  for (QScreen* screen : QGuiApplication::screens()) {
    if (screen && screen->availableGeometry().size().expandedTo(saved_size.size) == screen->availableGeometry().size()) {
      return true;
    }
  }

  return false;
}

std::optional<SavedWindowSize> loadSavedWindowSize() {
  QSettings settings;
  const auto has_size = settings.contains(QLatin1StringView{kWindowWidthKey})
      && settings.contains(QLatin1StringView{kWindowHeightKey});
  if (!has_size) {
    return std::nullopt;
  }

  const SavedWindowSize saved_size{
      .size = QSize{
          settings.value(QLatin1StringView{kWindowWidthKey}).toInt(),
          settings.value(QLatin1StringView{kWindowHeightKey}).toInt(),
      },
  };

  if (!isSavedWindowSizeValid(saved_size)) {
    return std::nullopt;
  }

  return saved_size;
}

void restoreWindowSize(QWindow* window, const SavedWindowSize& saved_size) {
  if (!window) {
    return;
  }

  window->resize(saved_size.size);
}

void saveWindowSize(const QWindow* window) {
  if (!window) {
    return;
  }

  const QSize size = window->size();
  if (!size.isValid()) {
    return;
  }

  QSettings settings;
  settings.setValue(QLatin1StringView{kWindowWidthKey}, size.width());
  settings.setValue(QLatin1StringView{kWindowHeightKey}, size.height());
  settings.sync();
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("The Last Viking LTD"));
#ifdef NDEBUG
  QCoreApplication::setApplicationName(QStringLiteral("lgx"));
#else
  QCoreApplication::setApplicationName(QStringLiteral("lgx-debug"));
#endif
  QCoreApplication::setApplicationVersion(lgx::applicationVersion());

  lgx::LoggingController loggingController;
  loggingController.initialize();
  registerApplicationFonts();
  LOG_INFO << "Starting lgx " << lgx::applicationVersionView();
  LOG_INFO << "Configuration from '" << loggingController.settingsFilePath().toStdString() << "'";

  const auto saved_window_size = loadSavedWindowSize();

  QQmlApplicationEngine engine;
  lgx::AppInfo appInfo;
  auto& appEngine = lgx::AppEngine::instance();
  auto& uiSettings = lgx::UiSettings::instance();
  appEngine.restoreSavedSession();
  if (saved_window_size.has_value()) {
    engine.setInitialProperties({
        {QStringLiteral("width"), saved_window_size->size.width()},
        {QStringLiteral("height"), saved_window_size->size.height()},
    });
  }
  engine.rootContext()->setContextProperty(QStringLiteral("AppInfo"), &appInfo);
  engine.rootContext()->setContextProperty(QStringLiteral("AppEngine"), &appEngine);
  engine.rootContext()->setContextProperty(QStringLiteral("UiSettings"), &uiSettings);
  QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                   [](const QList<QQmlError>& warnings) {
                     for (const auto& warning : warnings) {
                       LOG_ERROR << "QML: " << warning.toString().toStdString();
                     }
                   });
  QObject::connect(
    &engine,
    &QQmlApplicationEngine::objectCreationFailed,
    &app,
    []() { QCoreApplication::exit(EXIT_FAILURE); },
    Qt::QueuedConnection);
  engine.loadFromModule("lgx", "Main");
  if (engine.rootObjects().isEmpty()) {
    return EXIT_FAILURE;
  }

  auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
  if (saved_window_size.has_value()) {
    restoreWindowSize(window, *saved_window_size);
  }
  if (window) {
    window->show();
  }

  const QPointer<QWindow> window_guard(window);
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [window_guard, &appEngine]() {
    appEngine.saveSessionState();
    saveWindowSize(window_guard);
  });

  return app.exec();
}
