# Third-party dependencies

No dependency binaries are committed to this repository.

## Vendored Dear ImGui

- Version: `v1.91.8` (tag `v1.91.8`)
- Source: `https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.zip`
- SHA-256 of the downloaded archive: `C17AA6ABE51DDA39B0EA503B855B4A819A30D3AFFA6D53FB887541C29D686B98`
- License: MIT, see `third_party/imgui/LICENSE.txt`
- Vendored files: core sources plus `backends/imgui_impl_sdl2.*` and `backends/imgui_impl_opengl3.*`.
- CMake target: `camera_imgui`, built only when SDL2, OpenGL and ImGui are available.

## Windows laptop UI route

CMake tries `find_package(SDL2 QUIET)`, `find_package(OpenGL QUIET)`, and locates `imgui.h` (vendored or through vcpkg headers). If any dependency is missing, configure still succeeds, `CAMERA_CPP_HAS_LAPTOP_DEPS=OFF`, and the laptop target is skipped with:

```text
[laptop] deps missing; laptop target skipped (see third_party/THIRD_PARTY.md)
```

MSYS2 provides SDL2 and OpenGL; Dear ImGui is vendored, so no extra ImGui package is required.

Optional vcpkg route:

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
