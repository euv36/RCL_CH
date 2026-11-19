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

if(OpenGL_FOUND OR TARGET OpenGL::GL)
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
  message(STATUS "[laptop] deps found (Dear ImGui at ${IMGUI_INCLUDE_DIR}); laptop UI target can be built")

  if(TARGET SDL2::SDL2)
    set(_camera_cpp_sdl2_target SDL2::SDL2)
  else()
    set(_camera_cpp_sdl2_target ${SDL2_LIBRARIES})
  endif()

  if(TARGET OpenGL::GL)
    set(_camera_cpp_opengl_target OpenGL::GL)
  else()
    set(_camera_cpp_opengl_target ${OPENGL_LIBRARIES})
  endif()

  # Vendored Dear ImGui is the default route. It is compiled once and shared by
  # the laptop application and any UI smoke tests.
  if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp")
    add_library(camera_imgui STATIC
      "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp"
      "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp"
      "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp"
      "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp"
      "${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl2.cpp"
      "${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_opengl3.cpp"
    )
    target_include_directories(camera_imgui PUBLIC
      "${CMAKE_SOURCE_DIR}/third_party/imgui"
      "${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
    )
    target_link_libraries(camera_imgui PUBLIC ${_camera_cpp_sdl2_target} ${_camera_cpp_opengl_target})
    set(CAMERA_CPP_HAS_VENDORED_IMGUI 1)
  else()
    set(CAMERA_CPP_HAS_VENDORED_IMGUI 0)
  endif()
else()
  set(CAMERA_CPP_HAS_LAPTOP_DEPS OFF CACHE BOOL "SDL2, OpenGL, and Dear ImGui are available" FORCE)
  set(CAMERA_CPP_LAPTOP_DEPS_VALUE 0)
  set(CAMERA_CPP_HAS_VENDORED_IMGUI 0)
  message(STATUS "[laptop] deps missing; laptop target skipped (see third_party/THIRD_PARTY.md)")
endif()
