option(CAMERA_CPP_WERROR "Treat compiler warnings as errors" OFF)

add_library(camera_warnings INTERFACE)

if(MSVC)
  # /utf-8 keeps source/execution encodings consistent on Windows.
  target_compile_options(camera_warnings INTERFACE /W4 /permissive- /utf-8)
  if(CAMERA_CPP_WERROR)
    target_compile_options(camera_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(camera_warnings INTERFACE -Wall -Wextra -pedantic -Wconversion -Wshadow)
  if(CAMERA_CPP_WERROR)
    target_compile_options(camera_warnings INTERFACE -Werror)
  endif()
endif()
