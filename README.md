# camera_cpp

Native C++ rewrite of the gate-tracker stack. This is the approved isolated project; the original `C:\Users\sweetener\Desktop\camera` tree is reference material and remains untouched.

## Current state

This branch is under construction. See `docs/superpowers/plans/2026-11-19-native-optimization.md` for the implementation plan and `docs/superpowers/specs/2026-11-18-native-optimization-design.md` for the design.

## Build (Windows)

```powershell
$env:Path = "C:\Users\sweetener\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;" + $env:Path
& "C:\Program Files\CMake\bin\cmake.exe" -S . -B build-windows-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
& "C:\Program Files\CMake\bin\cmake.exe" --build build-windows-release
ctest --test-dir build-windows-release --output-on-failure
```

## Build (Pi)

See `docs/BUILD.md` and `docs/DEPLOY.md`.
