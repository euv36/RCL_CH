add_library(camera_warnings INTERFACE)

if(MSVC)
  target_compile_options(camera_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(camera_warnings INTERFACE -Wall -Wextra -pedantic -Wconversion -Wshadow)
endif()
