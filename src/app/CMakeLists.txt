qt_add_executable(lgx_app
  main.cpp
)

qt_add_qml_module(lgx_app
  URI lgx
  VERSION 1.0
  QML_FILES
    Main.qml
    qml/AboutDialog.qml
    qml/AdbSettings.qml
    qml/ColorSettings.qml
    qml/FilterView.qml
    qml/LogLineList.qml
    qml/LogWorkspace.qml
    qml/MarkedView.qml
    qml/components/LineMarkButton.qml
    qml/components/SymbolToolButton.qml
    qml/DialogHelper.js
    qml/GeneralSettings.qml
    qml/LoggingSettings.qml
    qml/PreferencesDialog.qml
    qml/LogView.qml
  RESOURCES
    qml/fonts/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf
)

target_link_libraries(lgx_app
  PRIVATE
    lgx::lib
    Qt6::Core
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
)

set_target_properties(lgx_app PROPERTIES
  OUTPUT_NAME lgx
)
