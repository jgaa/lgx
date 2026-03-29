add_library(lgx_lib
  logging.cpp
  util.cpp
  PageCache.cpp
  FileMonitor.cpp
  FileSource.cpp
  LogScanner.cpp
  LogModel.cpp
  OpenLogsModel.cpp
  RecentLogsModel.cpp
  AppEngine.cpp
  UiSettings.cpp
  util.h
  logging.h
  LogTypes.h
  PageCache.h
  FileMonitor.h
  FileSource.h
  LogScanner.h
  LogModel.h
  OpenLogsModel.h
  RecentLogsModel.h
  LogSource.h
  LogSource.cpp
  AppEngine.h
  UiSettings.h
)

add_library(lgx::lib ALIAS lgx_lib)

target_include_directories(lgx_lib
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(lgx_lib
  PUBLIC
    Qt6::Core
    QCoro6::Core
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    logfault::logfault
)

if(LGX_BOOST_TARGET)
  target_link_libraries(lgx_lib PUBLIC "${LGX_BOOST_TARGET}")
endif()
