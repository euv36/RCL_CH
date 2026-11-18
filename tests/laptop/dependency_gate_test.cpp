#if !defined(CAMERA_CPP_HAS_LAPTOP_DEPS)
#error "CAMERA_CPP_HAS_LAPTOP_DEPS must be defined by CMake"
#endif

static_assert(CAMERA_CPP_HAS_LAPTOP_DEPS == 0 || CAMERA_CPP_HAS_LAPTOP_DEPS == 1,
              "CAMERA_CPP_HAS_LAPTOP_DEPS must be 0 or 1");

int main() {
#if CAMERA_CPP_HAS_LAPTOP_DEPS
    return 0;
#else
    return 0;
#endif
}
