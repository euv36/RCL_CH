#if !defined(CAMERA_CPP_HAS_LAPTOP_DEPS)
#error "CAMERA_CPP_HAS_LAPTOP_DEPS must be defined by CMake"
#endif
#if !defined(CAMERA_CPP_HAS_VENDORED_IMGUI)
#error "CAMERA_CPP_HAS_VENDORED_IMGUI must be defined by CMake"
#endif

#if CAMERA_CPP_HAS_LAPTOP_DEPS
#include <imgui.h>
static_assert(IMGUI_VERSION_NUM >= 19180, "vendored Dear ImGui is too old");
#endif

static_assert(CAMERA_CPP_HAS_LAPTOP_DEPS == 0 || CAMERA_CPP_HAS_LAPTOP_DEPS == 1,
              "CAMERA_CPP_HAS_LAPTOP_DEPS must be 0 or 1");

int main() {
#if CAMERA_CPP_HAS_LAPTOP_DEPS
    return CAMERA_CPP_HAS_VENDORED_IMGUI ? 0 : 2;
#else
    return 0;
#endif
}
