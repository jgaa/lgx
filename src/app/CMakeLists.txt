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
    qml/components/PaneMenuButton.qml
    qml/components/SymbolToolButton.qml
    qml/DialogHelper.js
    qml/GeneralSettings.qml
    qml/LoggingSettings.qml
    qml/PreferencesDialog.qml
    qml/LogView.qml
  RESOURCES
    qml/fonts/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf
    qml/icons/tabler/LICENSE.txt
    qml/icons/tabler/chevrons-down.svg
    qml/icons/tabler/chevrons-up.svg
    qml/icons/tabler/device-mobile.svg
    qml/icons/tabler/error-next.svg
    qml/icons/tabler/error-previous.svg
    qml/icons/tabler/file-text.svg
    qml/icons/tabler/packages.svg
    qml/icons/tabler/player-skip-back.svg
    qml/icons/tabler/player-skip-forward.svg
    qml/icons/tabler/terminal-2.svg
    qml/icons/tabler/warning-next.svg
    qml/icons/tabler/warning-previous.svg
)

target_link_libraries(lgx_app
  PRIVATE
    lgx::lib
    Qt6::Core
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  get_target_property(LGX_QT_CORE_TARGET_TYPE Qt6::Core TYPE)
  if(LGX_QT_CORE_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
    set(LGX_QT_PLATFORM_PLUGINS)

    if(TARGET Qt6::QXcbIntegrationPlugin)
      list(APPEND LGX_QT_PLATFORM_PLUGINS Qt6::QXcbIntegrationPlugin)
    endif()

    if(TARGET Qt6::QWaylandIntegrationPlugin)
      list(APPEND LGX_QT_PLATFORM_PLUGINS Qt6::QWaylandIntegrationPlugin)
    endif()

    if(LGX_QT_PLATFORM_PLUGINS)
      qt_import_plugins(lgx_app INCLUDE ${LGX_QT_PLATFORM_PLUGINS})
    else()
      message(WARNING "Static Qt build detected, but no Linux Qt platform integration plugins were found.")
    endif()
  endif()
endif()

set_target_properties(lgx_app PROPERTIES
  OUTPUT_NAME lgx
)
