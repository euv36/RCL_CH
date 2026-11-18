find_package(SDL2 QUIET)
find_package(OpenGL QUIET)
find_path(IMGUI_INCLUDE_DIR
  NAMES imgui.h
  PATHS
    "${CMAKE_SOURCE_DIR}/third_party/imgui"
    "C:/vcpkg/installed/x64-windows/include"
    "/usr/include/imgui"
    "/usr/local/include/imgui"
)

if(SDL2_FOUND OR TARGET SDL2::SDL2)
  set(_camera_cpp_sdl2_ok TRUE)
else()
  set(_camera_cpp_sdl2_ok FALSE)
endif()

if(OpenGL_FOUND)
  set(_camera_cpp_opengl_ok TRUE)
else()
  set(_camera_cpp_opengl_ok FALSE)
endif()

if(IMGUI_INCLUDE_DIR)
  set(_camera_cpp_imgui_ok TRUE)
else()
  set(_camera_cpp_imgui_ok FALSE)
endif()

if(_camera_cpp_sdl2_ok AND _camera_cpp_opengl_ok AND _camera_cpp_imgui_ok)
  set(CAMERA_CPP_HAS_LAPTOP_DEPS ON CACHE BOOL "SDL2, OpenGL, and Dear ImGui are available" FORCE)
  set(CAMERA_CPP_LAPTOP_DEPS_VALUE 1)
  message(STATUS "[laptop] deps found; laptop UI target can be built")
else()
  set(CAMERA_CPP_HAS_LAPTOP_DEPS OFF CACHE BOOL "SDL2, OpenGL, and Dear ImGui are available" FORCE)
  set(CAMERA_CPP_LAPTOP_DEPS_VALUE 0)
  message(STATUS "[laptop] deps missing; laptop target skipped (see third_party/THIRD_PARTY.md)")
endif()
