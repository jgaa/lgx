add_library(lgx_lib
  AdbCandidatesModel.cpp
  AdbDevicesModel.cpp
  logging.cpp
  util.cpp
  PageCache.cpp
  FileMonitor.cpp
  FileSource.cpp
  DockerContainersModel.cpp
  FilterModel.cpp
  LogScanner.cpp
  LogModel.cpp
  MarkedModel.cpp
  OpenLogsModel.cpp
  RecentLogsModel.cpp
  StreamSource.cpp
  AppEngine.cpp
  UiSettings.cpp
  AdbCandidatesModel.h
  AdbDevicesModel.h
  util.h
  logging.h
  LogTypes.h
  PageCache.h
  FileMonitor.h
  FileSource.h
  DockerContainersModel.h
  FilterModel.h
  LogScanner.h
  LogModel.h
  MarkedModel.h
  OpenLogsModel.h
  RecentLogsModel.h
  StreamSource.h
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

if(LGX_ENABLE_SYSTEMD_SOURCE)
  target_compile_definitions(lgx_lib PUBLIC LGX_ENABLE_SYSTEMD_SOURCE=1)
  target_link_libraries(lgx_lib PUBLIC PkgConfig::SYSTEMD)
endif()
