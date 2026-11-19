# Dependency discovery is intentionally non-fatal at repository scaffold time.
# Platform-specific targets must check CAMERA_CPP_HAS_* and fail with a useful
# message if their required dependency is absent.  When the root project sets
# CAMERA_CPP_REQUIRE_RUNTIME_DEPS=ON, a missing/rejected dependency is fatal.
#
# Discovery uses only find_package(), pkg-config and CMAKE_PREFIX_PATH.  There
# are deliberately no hardcoded absolute paths: an absolute MSYS2 path used as
# a fallback could be picked up while compiling with WinLibs GCC, dragging a
# foreign C runtime into the build (corecrt_wstdlib.h / wchar.h redefinitions).
# Every accepted include dir / library is validated against the prefixes that
# belong to the active toolchain.

# ---------------------------------------------------------------------------
# Trusted roots for the active toolchain
# ---------------------------------------------------------------------------
set(CAMERA_CPP_TRUSTED_ROOTS "")

# Primary anchor: install prefix of the compiler actually driving the build
# (e.g. <prefix>/bin/g++.exe -> <prefix>; MSYS2 or WinLibs root alike).
if(CMAKE_CXX_COMPILER)
  get_filename_component(_camera_cpp_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(_camera_cpp_compiler_root "${_camera_cpp_bin_dir}" DIRECTORY)
  list(APPEND CAMERA_CPP_TRUSTED_ROOTS "${_camera_cpp_compiler_root}")
endif()

# The compiler's implicit include dirs reveal the same prefix and also cover
# compiler wrappers (ccache/launcher).  Their parents are trusted roots.
foreach(_camera_cpp_implicit_inc IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
  get_filename_component(_camera_cpp_implicit_root "${_camera_cpp_implicit_inc}" DIRECTORY)
  list(APPEND CAMERA_CPP_TRUSTED_ROOTS "${_camera_cpp_implicit_root}")
endforeach()

# Cross-compilation sysroots and user-requested prefixes are trusted by intent.
if(CMAKE_FIND_ROOT_PATH)
  list(APPEND CAMERA_CPP_TRUSTED_ROOTS ${CMAKE_FIND_ROOT_PATH})
endif()
if(CMAKE_PREFIX_PATH)
  list(APPEND CAMERA_CPP_TRUSTED_ROOTS ${CMAKE_PREFIX_PATH})
endif()

list(REMOVE_DUPLICATES CAMERA_CPP_TRUSTED_ROOTS)

# _camera_cpp_path_is_trusted(<out-var> <path>)
#   TRUE when <path> is under one of CAMERA_CPP_TRUSTED_ROOTS.  If no anchor is
#   known the check is a no-op so discovery can never be broken by the guard.
function(_camera_cpp_path_is_trusted _out _path)
  set(${_out} TRUE PARENT_SCOPE)
  if(NOT CAMERA_CPP_TRUSTED_ROOTS OR NOT _path)
    return()
  endif()

  file(TO_CMAKE_PATH "${_path}" _candidate)
  get_filename_component(_candidate "${_candidate}" ABSOLUTE)
  if(WIN32)
    string(TOLOWER "${_candidate}" _candidate)
  endif()

  foreach(_root IN LISTS CAMERA_CPP_TRUSTED_ROOTS)
    file(TO_CMAKE_PATH "${_root}" _trusted_root)
    get_filename_component(_trusted_root "${_trusted_root}" ABSOLUTE)
    if(WIN32)
      string(TOLOWER "${_trusted_root}" _trusted_root)
    endif()
    # Component-aware prefix test (avoids matching e.g. mingw64-other).
    cmake_path(IS_PREFIX _trusted_root "${_candidate}" NORMALIZE _prefix_match)
    if(_prefix_match)
      set(${_out} TRUE PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${_out} FALSE PARENT_SCOPE)
endfunction()

# _camera_cpp_dependency_message(<text>)
#   Respects the root-defined CAMERA_CPP_REQUIRE_RUNTIME_DEPS option: hard fail
#   in strict mode, warn otherwise.  The option is intentionally not defined
#   here; an unset value behaves like OFF.
function(_camera_cpp_dependency_message _text)
  if(CAMERA_CPP_REQUIRE_RUNTIME_DEPS)
    message(FATAL_ERROR "${_text}")
  else()
    message(WARNING "${_text}")
  endif()
endfunction()

# _camera_cpp_reject_if_foreign(<cache-var> <label>)
#   Clears <cache-var> when it points outside the active toolchain.
function(_camera_cpp_reject_if_foreign _var _label)
  if(NOT ${_var})
    return()
  endif()
  _camera_cpp_path_is_trusted(_trusted "${${_var}}")
  if(NOT _trusted)
    _camera_cpp_dependency_message(
      "Discarded ${_label} at '${${_var}}': outside the active compiler prefix. "
      "Point CMAKE_PREFIX_PATH / PKG_CONFIG_PATH at the matching toolchain.")
    unset(${_var} CACHE)
    set(${_var} "" PARENT_SCOPE)
  endif()
endfunction()

find_package(PkgConfig QUIET)

# ---------------------------------------------------------------------------
# OpenCV (OpenCV 5 config in MSYS2 lives at <prefix>/lib/cmake/opencv5)
# ---------------------------------------------------------------------------
find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs videoio highgui)
if(OpenCV_FOUND)
  set(_camera_cpp_opencv_trusted TRUE)

  _camera_cpp_path_is_trusted(_trusted "${OpenCV_DIR}")
  if(NOT _trusted)
    _camera_cpp_dependency_message("Discarded OpenCV config at '${OpenCV_DIR}': outside the active compiler prefix.")
    unset(OpenCV_DIR CACHE)
    set(_camera_cpp_opencv_trusted FALSE)
  endif()

  foreach(_opencv_inc IN LISTS OpenCV_INCLUDE_DIRS)
    _camera_cpp_path_is_trusted(_trusted "${_opencv_inc}")
    if(NOT _trusted)
      _camera_cpp_dependency_message("Discarded OpenCV include dir '${_opencv_inc}': outside the active compiler prefix.")
      set(_camera_cpp_opencv_trusted FALSE)
    endif()
  endforeach()

  if(_camera_cpp_opencv_trusted)
    set(CAMERA_CPP_HAS_OPENCV ON)
    message(STATUS "OpenCV found: ${OpenCV_VERSION}")
  else()
    set(CAMERA_CPP_HAS_OPENCV OFF)
    _camera_cpp_dependency_message("OpenCV was found but rejected because it does not belong to the active toolchain.")
  endif()
else()
  set(CAMERA_CPP_HAS_OPENCV OFF)
  # Fallback for toolchains without an OpenCV config: use pkg-config hints
  # instead of hardcoded absolute locations.
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_OPENCV QUIET opencv4)
  endif()
  find_library(CAMERA_CPP_OPENCV_CORE NAMES opencv_core HINTS ${PC_OPENCV_LIBRARY_DIRS})
  _camera_cpp_reject_if_foreign(CAMERA_CPP_OPENCV_CORE "opencv_core library")
  if(CAMERA_CPP_OPENCV_CORE)
    message(STATUS "OpenCV find_package failed, but opencv_core was found at ${CAMERA_CPP_OPENCV_CORE}")
  endif()
  _camera_cpp_dependency_message("OpenCV not found. Vision/Pi targets will be unavailable until OpenCV is installed (set OpenCV_DIR or CMAKE_PREFIX_PATH to the matching toolchain prefix).")
endif()

# ---------------------------------------------------------------------------
# libzmq
# ---------------------------------------------------------------------------
find_package(ZMQ QUIET)

# pkg-config is preferred over absolute paths; it honours PKG_CONFIG_PATH and
# the active toolchain's .pc files.
if(NOT ZMQ_FOUND AND PkgConfig_FOUND)
  pkg_check_modules(PC_ZMQ QUIET libzmq)
endif()

if(NOT ZMQ_FOUND)
  find_path(CAMERA_CPP_ZMQ_INCLUDE_DIR NAMES zmq.h HINTS ${PC_ZMQ_INCLUDE_DIRS})
  find_library(CAMERA_CPP_ZMQ_LIBRARY NAMES zmq libzmq HINTS ${PC_ZMQ_LIBRARY_DIRS})

  _camera_cpp_reject_if_foreign(CAMERA_CPP_ZMQ_INCLUDE_DIR "libzmq include dir")
  _camera_cpp_reject_if_foreign(CAMERA_CPP_ZMQ_LIBRARY "libzmq library")

  if(CAMERA_CPP_ZMQ_LIBRARY AND CAMERA_CPP_ZMQ_INCLUDE_DIR)
    set(ZMQ_FOUND TRUE)
    set(ZMQ_INCLUDE_DIRS ${CAMERA_CPP_ZMQ_INCLUDE_DIR})
    set(ZMQ_LIBRARIES ${CAMERA_CPP_ZMQ_LIBRARY})
  endif()
endif()

if(ZMQ_FOUND)
  set(_camera_cpp_zmq_trusted TRUE)

  _camera_cpp_path_is_trusted(_trusted "${ZMQ_DIR}")
  if(NOT _trusted)
    _camera_cpp_dependency_message("Discarded ZMQ config at '${ZMQ_DIR}': outside the active compiler prefix.")
    unset(ZMQ_DIR CACHE)
    set(_camera_cpp_zmq_trusted FALSE)
  endif()

  foreach(_zmq_inc IN LISTS ZMQ_INCLUDE_DIRS)
    _camera_cpp_path_is_trusted(_trusted "${_zmq_inc}")
    if(NOT _trusted)
      _camera_cpp_dependency_message("Discarded libzmq include dir '${_zmq_inc}': outside the active compiler prefix.")
      set(_camera_cpp_zmq_trusted FALSE)
    endif()
  endforeach()

  if(_camera_cpp_zmq_trusted)
    set(CAMERA_CPP_HAS_ZMQ ON)
    message(STATUS "libzmq found")
  else()
    set(CAMERA_CPP_HAS_ZMQ OFF)
    _camera_cpp_dependency_message("libzmq was found but rejected because it does not belong to the active toolchain.")
  endif()
else()
  set(CAMERA_CPP_HAS_ZMQ OFF)
  _camera_cpp_dependency_message("libzmq not found. Pi/laptop messaging targets will be unavailable until libzmq is installed (set ZMQ_DIR or CMAKE_PREFIX_PATH to the matching toolchain prefix).")
endif()
