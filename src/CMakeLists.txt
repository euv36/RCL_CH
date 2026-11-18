add_library(camera_common STATIC
  common/atomic_file.cpp
  common/json_config.cpp
  common/paths.cpp
  common/logging.cpp
)
target_link_libraries(camera_common PUBLIC camera_warnings)
target_include_directories(camera_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_SOURCE_DIR}/third_party)

add_library(camera_vision STATIC
  vision/hsv_color.cpp
)
target_link_libraries(camera_vision PUBLIC camera_common)
target_include_directories(camera_vision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vision ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(kCameraCppSmoke common/smoke.cpp)
target_link_libraries(kCameraCppSmoke PRIVATE camera_warnings)

add_executable(kCameraCppAtomicFileTest ../tests/common/atomic_file_test.cpp)
target_link_libraries(kCameraCppAtomicFileTest PRIVATE camera_common)
add_test(NAME atomic_file COMMAND kCameraCppAtomicFileTest)

add_executable(kCameraCppJsonConfigTest ../tests/common/json_config_test.cpp)
target_link_libraries(kCameraCppJsonConfigTest PRIVATE camera_common)
add_test(NAME json_config COMMAND kCameraCppJsonConfigTest)

add_executable(kCameraCppHsvColorTest ../tests/vision/hsv_color_test.cpp)
target_link_libraries(kCameraCppHsvColorTest PRIVATE camera_vision)
add_test(NAME hsv_color COMMAND kCameraCppHsvColorTest)

add_executable(kCameraCppParityRunner ../tests/parity/parity_runner.cpp)
target_link_libraries(kCameraCppParityRunner PRIVATE camera_vision)
add_test(NAME hsv_parity COMMAND kCameraCppParityRunner)

add_executable(kCameraCppSmokeTest ../tests/common/smoke_test.cpp)
target_link_libraries(kCameraCppSmokeTest PRIVATE camera_warnings)
add_test(NAME common_smoke COMMAND kCameraCppSmokeTest)
