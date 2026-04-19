cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

project(lgx VERSION 0.2.6 LANGUAGES CXX)

add_definitions(-DLGX_VERSION=\"${CMAKE_PROJECT_VERSION}\")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/flatpak/eu.lastviking.lgx.metainfo.xml.in"
  "${CMAKE_CURRENT_BINARY_DIR}/flatpak/eu.lastviking.lgx.metainfo.xml"
  @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(LGX_ENABLE_TESTS "Build tests and enable CTest integration" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LGX_ENABLE_SYSTEMD_SOURCE_DEFAULT ON)
else()
  set(LGX_ENABLE_SYSTEMD_SOURCE_DEFAULT OFF)
endif()
option(LGX_ENABLE_SYSTEMD_SOURCE "Build the systemd journal stream source" ${LGX_ENABLE_SYSTEMD_SOURCE_DEFAULT})

set(BUILD_TESTING ${LGX_ENABLE_TESTS} CACHE BOOL "Build tests" FORCE)
set(QCORO_WITH_QTDBUS OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTNETWORK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTWEBSOCKETS OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTQUICK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QML OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTTEST OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)
include(LgxFetch)

lgx_fetch(qcoro
  GIT_REPOSITORY https://github.com/qcoro/qcoro.git
  GIT_TAG        v0.12.0
)

lgx_fetch(logfault
  GIT_REPOSITORY https://github.com/jgaa/logfault.git
  GIT_TAG        v0.8.5
)

if(LGX_ENABLE_TESTS)
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    lgx_fetch(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        v1.16.0
    )
  endif()
endif()

find_package(Boost REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Quick Qml Svg Widgets)
if(LGX_ENABLE_SYSTEMD_SOURCE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
endif()

qt_standard_project_setup(REQUIRES 6.8)

if(TARGET Boost::headers)
  set(LGX_BOOST_TARGET Boost::headers)
elseif(TARGET Boost::boost)
  set(LGX_BOOST_TARGET Boost::boost)
else()
  set(LGX_BOOST_TARGET "")
endif()

add_subdirectory(src)

if(LGX_ENABLE_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
