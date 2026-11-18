add_library(camera_common STATIC
  common/atomic_file.cpp
  common/json_config.cpp
  common/paths.cpp
  common/logging.cpp
  common/gate_store.cpp
  common/field_map.cpp
  common/cam_settings_config.cpp
)
target_link_libraries(camera_common PUBLIC camera_warnings)
target_include_directories(camera_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_SOURCE_DIR}/third_party)

add_library(camera_vision STATIC
  vision/distance.cpp
  vision/hsv_color.cpp
  vision/gate_cluster.cpp
)
target_link_libraries(camera_vision PUBLIC camera_common)
target_include_directories(camera_vision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vision ${CMAKE_CURRENT_SOURCE_DIR})

if(CAMERA_CPP_HAS_OPENCV)
add_library(camera_vision_cv STATIC vision/self_mask.cpp vision/tracking.cpp vision/calibration.cpp)
target_link_libraries(camera_vision_cv PUBLIC camera_warnings camera_vision ${OpenCV_LIBS})
target_include_directories(camera_vision_cv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vision ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
endif()

add_executable(kCameraCppSmoke common/smoke.cpp)
target_link_libraries(kCameraCppSmoke PRIVATE camera_warnings)

add_executable(kCameraCppAtomicFileTest ../tests/common/atomic_file_test.cpp)
target_link_libraries(kCameraCppAtomicFileTest PRIVATE camera_common)
add_test(NAME atomic_file COMMAND kCameraCppAtomicFileTest)

add_executable(kCameraCppJsonConfigTest ../tests/common/json_config_test.cpp)
target_link_libraries(kCameraCppJsonConfigTest PRIVATE camera_common)
add_test(NAME json_config COMMAND kCameraCppJsonConfigTest)

add_executable(kCameraCppGateStoreTest ../tests/common/gate_store_test.cpp)
target_link_libraries(kCameraCppGateStoreTest PRIVATE camera_common)
add_test(NAME gate_store COMMAND kCameraCppGateStoreTest)

add_executable(kCameraCppFieldMapTest ../tests/common/field_map_test.cpp)
target_link_libraries(kCameraCppFieldMapTest PRIVATE camera_common)
add_test(NAME field_map COMMAND kCameraCppFieldMapTest)

add_executable(kCameraCppCamSettingsConfigTest ../tests/common/cam_settings_config_test.cpp)
target_link_libraries(kCameraCppCamSettingsConfigTest PRIVATE camera_common)
add_test(NAME cam_settings_config COMMAND kCameraCppCamSettingsConfigTest)

add_executable(kCameraCppHsvColorTest ../tests/vision/hsv_color_test.cpp)
target_link_libraries(kCameraCppHsvColorTest PRIVATE camera_vision)
add_test(NAME hsv_color COMMAND kCameraCppHsvColorTest)

add_executable(kCameraCppGateClusterTest ../tests/vision/gate_cluster_test.cpp)
target_link_libraries(kCameraCppGateClusterTest PRIVATE camera_vision)
add_test(NAME gate_cluster COMMAND kCameraCppGateClusterTest)

add_executable(kCameraCppDistanceTest ../tests/vision/distance_test.cpp)
target_link_libraries(kCameraCppDistanceTest PRIVATE camera_vision)
add_test(NAME distance COMMAND kCameraCppDistanceTest)

if(CAMERA_CPP_HAS_OPENCV)
add_executable(kCameraCppSelfMaskTest ../tests/vision/self_mask_test.cpp)
target_link_libraries(kCameraCppSelfMaskTest PRIVATE camera_vision_cv)
add_test(NAME self_mask COMMAND kCameraCppSelfMaskTest)

add_executable(kCameraCppTrackingTest ../tests/vision/tracking_test.cpp)
target_link_libraries(kCameraCppTrackingTest PRIVATE camera_vision_cv)
add_test(NAME tracking COMMAND kCameraCppTrackingTest)
endif()

add_executable(kCameraCppParityRunner ../tests/parity/parity_runner.cpp)
target_link_libraries(kCameraCppParityRunner PRIVATE camera_vision)
add_test(NAME hsv_parity COMMAND kCameraCppParityRunner)

add_executable(kCameraCppClusterParity ../tests/parity/cluster_parity.cpp)
target_link_libraries(kCameraCppClusterParity PRIVATE camera_vision)
add_test(NAME cluster_parity COMMAND kCameraCppClusterParity)

add_executable(kCameraCppSmokeTest ../tests/common/smoke_test.cpp)
target_link_libraries(kCameraCppSmokeTest PRIVATE camera_warnings)
add_test(NAME common_smoke COMMAND kCameraCppSmokeTest)
