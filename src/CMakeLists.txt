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
target_include_directories(camera_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common ${PROJECT_SOURCE_DIR}/third_party)

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
add_test(NAME common_smoke_binary COMMAND kCameraCppSmoke)

add_executable(kCameraCppAtomicFileTest ../tests/common/atomic_file_test.cpp)
target_link_libraries(kCameraCppAtomicFileTest PRIVATE camera_common)
add_test(NAME atomic_file COMMAND kCameraCppAtomicFileTest)

add_executable(kCameraCppJsonConfigTest ../tests/common/json_config_test.cpp)
target_link_libraries(kCameraCppJsonConfigTest PRIVATE camera_common)
add_test(NAME json_config COMMAND kCameraCppJsonConfigTest)

if(CAMERA_CPP_HAS_OPENCV)
add_executable(kCameraCppCaptureConfigTest ../tests/pi_camera/capture_config_test.cpp)
add_executable(kCameraCppDownscaleReuseTest ../tests/pi_camera/downscale_reuse_test.cpp)
add_library(camera_pi_camera STATIC
  pi_camera/v4l2_capture.cpp
  pi_camera/camera_engine.cpp
)
target_link_libraries(camera_pi_camera PUBLIC camera_vision_cv ${OpenCV_LIBS} camera_warnings)
target_include_directories(camera_pi_camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/pi_camera ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(kCameraCppCaptureConfigTest PRIVATE camera_pi_camera)
add_test(NAME capture_config COMMAND kCameraCppCaptureConfigTest)
target_link_libraries(kCameraCppDownscaleReuseTest PRIVATE camera_pi_camera)
add_test(NAME downscale_reuse COMMAND kCameraCppDownscaleReuseTest)

# Headless-проба локальной камеры: собирается, но не регистрируется в ctest,
# потому что требует реального USB-устройства и не должна падать в CI без него.
add_executable(local_camera_probe ../tools/local_camera_probe.cpp)
target_link_libraries(local_camera_probe PRIVATE camera_pi_camera)

if(CAMERA_CPP_HAS_LAPTOP_DEPS)
add_executable(local_camera_preview ../tools/local_camera_preview.cpp)
target_link_libraries(local_camera_preview PRIVATE camera_pi_camera camera_laptop_preview ${_camera_cpp_sdl2_target})
target_include_directories(local_camera_preview PRIVATE ${SDL2_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})
endif()
endif()

add_library(camera_pi_logic STATIC
  pi_camera/status_builder.cpp
  pi_camera/cam_settings.cpp
  pi_camera/serial_link.cpp
)
target_link_libraries(camera_pi_logic PUBLIC camera_common)
target_include_directories(camera_pi_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/pi_camera ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common ${PROJECT_SOURCE_DIR}/third_party)

add_executable(kCameraCppStatusBuilderTest ../tests/pi_camera/status_builder_test.cpp)
target_link_libraries(kCameraCppStatusBuilderTest PRIVATE camera_pi_logic)
add_test(NAME status_builder COMMAND kCameraCppStatusBuilderTest)

add_executable(kCameraCppCamSettingsTest ../tests/pi_camera/cam_settings_test.cpp)
target_link_libraries(kCameraCppCamSettingsTest PRIVATE camera_pi_logic)
add_test(NAME cam_settings COMMAND kCameraCppCamSettingsTest)

add_executable(kCameraCppSerialLinkTest ../tests/pi_camera/serial_link_test.cpp)
target_link_libraries(kCameraCppSerialLinkTest PRIVATE camera_pi_logic)
add_test(NAME serial_link COMMAND kCameraCppSerialLinkTest)

if(CAMERA_CPP_HAS_ZMQ)
add_library(camera_pi_zmq STATIC
  pi_camera/color_listener.cpp
)
target_link_libraries(camera_pi_zmq PUBLIC camera_pi_logic camera_common Threads::Threads ${ZMQ_LIBRARIES})
target_include_directories(camera_pi_zmq PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/pi_camera ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common ${PROJECT_SOURCE_DIR}/third_party ${ZMQ_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIR})
endif()

if(CAMERA_CPP_HAS_OPENCV AND CAMERA_CPP_HAS_ZMQ)
add_executable(camera_service pi_camera/service_main.cpp)
target_link_libraries(camera_service PRIVATE camera_pi_camera camera_pi_zmq camera_pi_logic camera_common Threads::Threads ${OpenCV_LIBS} ${ZMQ_LIBRARIES})
target_include_directories(camera_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/pi_camera ${PROJECT_SOURCE_DIR}/third_party ${OpenCV_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIR})

add_executable(kCameraCppPiZmqLoopbackTest ../tests/integration/pi_zmq_loopback_test.cpp)
target_link_libraries(kCameraCppPiZmqLoopbackTest PRIVATE camera_common Threads::Threads ${ZMQ_LIBRARIES})
target_include_directories(kCameraCppPiZmqLoopbackTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common ${PROJECT_SOURCE_DIR}/third_party ${ZMQ_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIR})
target_compile_definitions(kCameraCppPiZmqLoopbackTest PRIVATE CAMERA_SERVICE_PATH="$<TARGET_FILE:camera_service>")
add_test(NAME pi_zmq_loopback COMMAND kCameraCppPiZmqLoopbackTest)
endif()

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

add_library(camera_laptop_core STATIC
  laptop/state.cpp
  laptop/source_controller.cpp
  laptop/clipboard_text.cpp
  laptop/panels.cpp
  laptop/help_ru.cpp
  laptop/radar_panel.cpp
  laptop/cam_settings_panel.cpp
  laptop/theme.cpp
)
target_include_directories(camera_laptop_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/laptop ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(camera_laptop_core PUBLIC camera_warnings)

add_executable(kCameraCppClipboardTest ../tests/laptop/clipboard_test.cpp)
target_link_libraries(kCameraCppClipboardTest PRIVATE camera_laptop_core)
add_test(NAME clipboard COMMAND kCameraCppClipboardTest)

add_executable(kCameraCppSourceControllerTest ../tests/laptop/source_controller_test.cpp)
target_link_libraries(kCameraCppSourceControllerTest PRIVATE camera_laptop_core)
add_test(NAME source_controller COMMAND kCameraCppSourceControllerTest)

add_executable(kCameraCppHelpRuTest ../tests/laptop/help_ru_test.cpp)
target_link_libraries(kCameraCppHelpRuTest PRIVATE camera_laptop_core)
add_test(NAME help_ru COMMAND kCameraCppHelpRuTest)

if(CAMERA_CPP_HAS_OPENCV)
add_library(camera_laptop_hsv STATIC laptop/hsv_panel.cpp)
target_link_libraries(camera_laptop_hsv PUBLIC camera_laptop_core ${OpenCV_LIBS})
target_include_directories(camera_laptop_hsv PUBLIC ${OpenCV_INCLUDE_DIRS})

add_executable(kCameraCppHsvPanelTest ../tests/laptop/hsv_panel_test.cpp)
target_link_libraries(kCameraCppHsvPanelTest PRIVATE camera_laptop_hsv)
add_test(NAME hsv_panel COMMAND kCameraCppHsvPanelTest)

add_library(camera_laptop_preview STATIC laptop/preview_support.cpp)
target_link_libraries(camera_laptop_preview PUBLIC camera_laptop_core ${OpenCV_LIBS})
target_include_directories(camera_laptop_preview PUBLIC ${OpenCV_INCLUDE_DIRS})

add_executable(kCameraCppPreviewSupportTest ../tests/laptop/preview_support_test.cpp)
target_link_libraries(kCameraCppPreviewSupportTest PRIVATE camera_laptop_preview)
add_test(NAME preview_support COMMAND kCameraCppPreviewSupportTest)
endif()

if(CAMERA_CPP_HAS_OPENCV AND CAMERA_CPP_HAS_ZMQ)
add_library(camera_laptop_video STATIC laptop/pi_video_client.cpp)
target_link_libraries(camera_laptop_video PUBLIC camera_warnings ${OpenCV_LIBS} ${ZMQ_LIBRARIES})
target_include_directories(camera_laptop_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  ${OpenCV_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIR})

add_executable(kCameraCppPiVideoClientTest ../tests/laptop/pi_video_client_test.cpp)
target_link_libraries(kCameraCppPiVideoClientTest PRIVATE camera_laptop_video)
add_test(NAME pi_video_client COMMAND kCameraCppPiVideoClientTest)
endif()

add_executable(kCameraCppLayoutSmokeTest ../tests/laptop/layout_smoke_test.cpp)
target_link_libraries(kCameraCppLayoutSmokeTest PRIVATE camera_laptop_core)
add_test(NAME layout_smoke COMMAND kCameraCppLayoutSmokeTest)

add_executable(kCameraCppRadarPanelTest ../tests/laptop/radar_panel_test.cpp)
target_link_libraries(kCameraCppRadarPanelTest PRIVATE camera_laptop_core)
add_test(NAME radar_panel COMMAND kCameraCppRadarPanelTest)

add_executable(kCameraCppCamSettingsPanelTest ../tests/laptop/cam_settings_panel_test.cpp)
target_link_libraries(kCameraCppCamSettingsPanelTest PRIVATE camera_laptop_core)
add_test(NAME cam_settings_panel COMMAND kCameraCppCamSettingsPanelTest)

add_executable(kCameraCppThemeTest ../tests/laptop/theme_test.cpp)
target_link_libraries(kCameraCppThemeTest PRIVATE camera_laptop_core)
add_test(NAME theme COMMAND kCameraCppThemeTest)

if(CAMERA_CPP_HAS_LAPTOP_DEPS AND TARGET camera_imgui)
  add_executable(camera_laptop_ui
    laptop/main.cpp
    laptop/app.cpp
    laptop/widgets.cpp
    laptop/clipboard.cpp
  )
  target_link_libraries(camera_laptop_ui PRIVATE camera_laptop_core camera_common camera_imgui)
  target_include_directories(camera_laptop_ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/laptop
    ${SDL2_INCLUDE_DIRS}
  )
  if(CAMERA_CPP_HAS_ZMQ)
    target_compile_definitions(camera_laptop_ui PRIVATE CAMERA_CPP_HAS_ZMQ=1)
    target_include_directories(camera_laptop_ui PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(camera_laptop_ui PRIVATE ${ZMQ_LIBRARIES})
  endif()
endif()
