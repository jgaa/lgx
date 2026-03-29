include(FetchContent)

function(lgx_fetch name)
  set(options)
  set(oneValueArgs SOURCE_DIR GIT_REPOSITORY GIT_TAG)
  set(multiValueArgs)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  string(TOUPPER "${name}" NAME_UP)
  string(REPLACE "-" "_" NAME_UP "${NAME_UP}")
  string(REPLACE "." "_" NAME_UP "${NAME_UP}")
  set(OVERRIDE_VAR "LGX_${NAME_UP}_SOURCE_DIR")

  set(${OVERRIDE_VAR} "" CACHE PATH "Use a local source dir for ${name}")

  if(${OVERRIDE_VAR})
    message(STATUS "LGX: Using local ${name} from ${${OVERRIDE_VAR}}")
    FetchContent_Declare(${name} SOURCE_DIR "${${OVERRIDE_VAR}}")
  else()
    FetchContent_Declare(${name}
      GIT_REPOSITORY "${ARG_GIT_REPOSITORY}"
      GIT_TAG        "${ARG_GIT_TAG}"
    )
  endif()

  set(_lgx_prev_skip_install_rules "${CMAKE_SKIP_INSTALL_RULES}")
  set(CMAKE_SKIP_INSTALL_RULES ON)
  FetchContent_MakeAvailable(${name})
  if(DEFINED _lgx_prev_skip_install_rules)
    set(CMAKE_SKIP_INSTALL_RULES "${_lgx_prev_skip_install_rules}")
  else()
    unset(CMAKE_SKIP_INSTALL_RULES)
  endif()

  set(_lgx_dep_bin_dir "${${name}_BINARY_DIR}")
  if(_lgx_dep_bin_dir)
    set(_lgx_dep_install_script "${_lgx_dep_bin_dir}/cmake_install.cmake")
    if(NOT EXISTS "${_lgx_dep_install_script}")
      file(MAKE_DIRECTORY "${_lgx_dep_bin_dir}")
      file(WRITE "${_lgx_dep_install_script}" "# intentionally empty\n")
    endif()
  endif()
endfunction()
