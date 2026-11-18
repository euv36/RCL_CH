add_executable(kCameraCppSmoke common/smoke.cpp)
target_link_libraries(kCameraCppSmoke PRIVATE camera_warnings)

add_executable(kCameraCppSmokeTest ../tests/common/smoke_test.cpp)
target_link_libraries(kCameraCppSmokeTest PRIVATE camera_warnings)
add_test(NAME common_smoke COMMAND kCameraCppSmokeTest)
