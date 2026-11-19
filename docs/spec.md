# Native Camera Optimization Design

**Date:** 2026-11-19  
**Status:** Draft, awaiting approval  
**Project size:** A new full implementation, not a small patch.

## Why this work exists

`C:\Users\sweetener\Desktop\camera` already contains a working camera/robot system. It is useful as a behavioral reference, but the current implementation does not provide the maximum possible performance, stability, and maintainability.

This work creates a faster, cleaner, native C++ version of the same system while preserving its current behavior.

## What will be built

A new project will be created at:

```text
C:\Users\sweetener\Desktop\camera_cpp
```

Branch and repository:

- branch: `native-optimization`
- new git repository
- local work only by default; GitHub publishing is a separate decision

The original repository at `C:\Users\sweetener\Desktop\camera` remains untouched and is used only as a reference.

## Required functionality

The native version must preserve the full current feature set:

- full-FOV 1920x1080 MJPG capture;
- software downscale/rotation with no sensor crop;
- G1/G2/G3 color gate tracking;
- gate clustering and false-positive rejection;
- self-mask;
- distance estimation;
- Pi multipart status stream over ZMQ;
- saved gate color persistence;
- laptop UI with:
  - CONNECT and Ctrl+V paste;
  - CAM SET;
  - RADAR;
  - BIND FIELD;
  - POS;
  - HSV MASK;
  - INFO;
  - SAVE G1/G2/G3;
  - camera source input;
- live CAM SET application on Pi through port 5559;
- robot core;
- Mega firmware integration;
- automated verification and hardware smoke tests.

## Goals

- Native C++ laptop application.
- Native C++ Pi camera service.
- Shared C++ core for tracking, protocols, and data types.
- No Python runtime dependency for normal laptop/Pi camera operation.
- Faster frame pipeline with reused buffers.
- No per-pixel loops in hot paths.
- SIMD-friendly OpenCV usage.
- Controlled threading with measured frame/capture/tracking costs.
- Reliable handling of invalid or missing configuration.
- Existing behavior protected by tests and parity checks.
- Overnight unattended execution where safely possible.
- Laptop sleep after final reporting.

## Non-goals

- Do not modify the original `C:\Users\sweetener\Desktop\camera` repository in place.
- Do not reset or clean its dirty working tree.
- Do not enable camera auto-exposure.
- Do not replace full-FOV capture with a cropped fast mode.
- Do not commit passwords, SSH keys, tokens, or credentials.
- Do not commit local calibration or runtime state files to git.
- Do not wait for overnight user interaction. If blocked, use conservative documented defaults.

## Technology choices

Chosen stack:

- C++17 or newer;
- CMake;
- SDL2 for the laptop window and input;
- Dear ImGui for the laptop UI;
- OpenCV for image processing;
- ZMQ/cppzmq for Pi-laptop messaging;
- V4L2 on Raspberry Pi for camera capture;
- C++ unit tests;
- Python/C++ parity checks where useful during migration.

The old Python project remains a reference, not a dependency.

## Repository shape

```text
camera_cpp/
  CMakeLists.txt
  CMakePresets.json
  cmake/
  src/
    common/        # JSON, atomic files, logging, config, protocols
    vision/        # tracking, clustering, masks, self-mask, distance
    pi_camera/     # V4L2 capture, CAM SET listener, status publisher
    laptop/        # SDL2 + Dear ImGui application
    robot_core/    # robot logic and tests
    mega_fw/       # firmware or port based on existing protocol
  tests/           # unit and parity tests
  assets/          # fixtures/images copied deliberately
  docs/            # build, deploy, verification docs
  third_party/     # vendored headers only when needed
```

## Build strategy

### Windows

Available on the user machine:

- CMake at `C:\Program Files\CMake\bin\cmake.exe`;
- WinLibs/MinGW GCC 16.2.0;
- winget.

Plan:

1. Prefer CMake discovery for OpenCV and ZMQ.
2. If discovery fails, use vcpkg in manifest mode.
3. If vcpkg is unavailable or unsuitable, use pinned prebuilt/portable OpenCV and ZMQ.
4. Install SDL2 and Dear ImGui through vcpkg or pinned vendored releases.
5. If a dependency cannot be made reproducible, block only the dependent task, document it, and continue independent work.

### Raspberry Pi

Already available:

- OpenCV 4.10.0;
- ZMQ 26.4.0;
- g++ 14.2.0;
- cmake 3.31.6;
- HD USB Camera at `/dev/video0` and `/dev/video1`.

The new Pi service will use system libraries and will not require a Python runtime.

## Pi access and end state

User decisions:

- SSH access as `twox@10.132.122.51` is available.
- The Pi is reachable on the same network.
- `sudo` is allowed on Pi when genuinely required.
- The user wants temporary Pi smoke testing first.

Pi smoke behavior:

1. Build and upload/run the new native service in a temporary location.
2. Verify camera capture, status stream, and CAM SET behavior.
3. Stop the new service after verification.
4. Do not overwrite Pi calibration/config unless required for the test.
5. If the new service cannot run safely, document the blocker and leave the Pi in a safe state.

## Robot and Mega firmware

User decision:

- Full flash is allowed.

Safe implementation rule:

1. Build and unit-test first.
2. Verify protocol parity before flashing.
3. Flash only with explicit logs, timeouts, and recovery/rollback notes.
4. If any step is unclear, stop and document it rather than guessing around hardware.

## Local runtime state

User decision:

- Seed existing local contract files into the new project as local non-git data.
- Do not push those files to GitHub.

Candidates:

- `saved_gates.json`
- `camera_settings.json`
- `field_map.json`
- `distance_calib.json`
- calibration/threshold data used by the current project

The new project must still run with defaults when these files are absent.

## UI information text

The implementation language is English.

The INFO window content describing application functions will be written in Russian, because the current user uses Russian and the current reference INFO content is Russian.

INFO must cover every important control and output:

- CONNECT and the source field;
- Ctrl+V;
- CAM SET and all camera controls;
- RADAR;
- BIND FIELD;
- POS;
- HSV MASK;
- INFO itself;
- SAVE G1/G2/G3;
- status and GATES panels;
- thresholds and gate configuration controls;
- angle/log/status outputs.

## Implementation rules

- Keep UI and camera/tracking work separate.
- UI renders from cached application state.
- UI callbacks do not directly own camera device handles.
- Reuse `cv::Mat` buffers where safe.
- Avoid unnecessary copies and temporary allocations.
- Avoid locks around complete frame processing.
- Keep Pi thread use bounded; the Pi has four CPU cores.
- No hand-written SIMD until profiling proves the need.
- Prefer OpenCV primitives: `cvtColor`, `inRange`, LUTs, morphology, connected components, moments, bitwise operations.
- Never silently enable auto-exposure.
- Invalid camera controls are clamped or rejected with truthful status.
- Invalid or missing config produces defaults and warnings, not a crash.
- Atomic writes preserve unknown config keys.

## Verification requirements

Before calling the work done, run and record:

- Windows build;
- Pi build;
- C++ unit tests;
- vision and tracking tests;
- protocol/JSON tests;
- persistence tests;
- distance, self-mask, and clustering tests;
- Python/C++ parity tests for critical algorithms;
- native regression checks for:
  - gate detection;
  - stale Pi gates;
  - camera apply/readback behavior;
  - HSV view alignment;
  - Ctrl+V;
  - radar toggle/close;
- live local ELP smoke;
- live Pi smoke;
- metrics report with FPS/CPU/timing.

Anything not verified must be reported as unverified, not described as green.

## Laptop sleep

After verification is green, or all feasible work is complete and blockers are documented:

1. write the final report;
2. confirm the repo/files are in the intended state;
3. put the laptop into normal Windows sleep mode.

Do not use hibernate unless explicitly requested. Sleep is the final action.

## GitHub

The new project is local by default. Do not push automatically.

If publishing is desired, decide after the implementation is green and reviewed.

