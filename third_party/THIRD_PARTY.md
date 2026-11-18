# Third-party dependencies

No dependency binaries are committed to this repository.

## Windows laptop UI route

CMake tries `find_package(SDL2 QUIET)`, `find_package(OpenGL QUIET)`, and locates `imgui.h` (typically through vcpkg headers). If any dependency is missing, configure still succeeds, `CAMERA_CPP_HAS_LAPTOP_DEPS=OFF`, and the laptop target is skipped with:

```text
[laptop] deps missing; laptop target skipped (see third_party/THIRD_PARTY.md)
```

Recommended Windows install:

```powershell
vcpkg install sdl2 imgui[sdl2-binding,opengl3-binding] --triplet x64-windows
```

Then configure with the vcpkg toolchain:

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" -S . -B build-windows-release -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
```

`fmt` is optional; the project does not require it for the laptop UI.

## Pi route

The Pi camera service uses system OpenCV and libzmq only. It does not require SDL2, OpenGL, or Dear ImGui.

## Vendored headers

`third_party/nlohmann/json.hpp` is the single-header nlohmann/json library copied from the legacy `pi_core/vendor` tree for offline compatibility. Its upstream license is embedded in the header. Do not add other third-party source without recording version, source URL, and license here.

## Binaries

Do not commit SDL2, OpenCV, ZMQ, or Dear ImGui binaries. If vendoring is ever required, add the exact upstream version, download URL, checksum, and license text before committing.
