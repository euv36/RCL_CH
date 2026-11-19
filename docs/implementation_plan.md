# Native Camera Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. This is a very large project with one shared contract; review each task before starting the next. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a new native C++ full-system replacement at `C:\Users\sweetener\Desktop\camera_cpp` that is faster, cleaner, and at least as reliable as the current camera/robot project, while leaving `C:\Users\sweetener\Desktop\camera` as an untouched behavioral reference.

**Architecture:** New git repository on branch `native-optimization`. Shared C++17 core under `src/common`, `src/vision`, `src/robot_core`. Native Pi camera service under `src/pi_camera` linked to shared core. Native SDL2 + Dear ImGui laptop application under `src/laptop` linked to shared core. `src/mega_fw` contains a tested protocol-compatible firmware port/build path. Tests live in `tests`; fixtures in `assets/fixtures`; build system CMake + presets.

**Tech Stack:** C++17, CMake, SDL2, Dear ImGui, OpenCV, ZMQ/cppzmq, V4L2, C++ unit tests, Python/C++ parity tools during migration, GitHub Actions-style local CI scripts only if useful, no Python runtime dependency for normal operation.

**Spec:** `C:\Users\sweetener\Desktop\camera\docs\superpowers\specs\2026-11-18-native-optimization-design.md`

## Global Constraints

- Repository: create `C:\Users\sweetener\Desktop\camera_cpp` as a **new** git repository; branch `native-optimization`; no commits to `C:\Users\sweetener\Desktop\camera`; original working tree remains read-only reference (except the already approved root design/plan docs).
- Capture must remain full-FOV `1920x1080 MJPG`; software downscale/rotation only; never replace with a cropped "fast mode".
- Pi working frame is `640x360`; laptop portrait processed frame is `720x1280`, 90 degrees CCW like the reference.
- Auto-exposure must never be enabled automatically; invalid camera controls are clamped or rejected with truthful status.
- No per-pixel hot loops; reuse `cv::Mat` buffers; prefer OpenCV `cvtColor`, `inRange`, morphology, connected components, moments, bitwise ops; no hand-written SIMD unless profiling proves it.
- Pi has four cores; bounded thread use; no locks around full frame processing.
- Ports/contracts: ZMQ video/status `5555`, laptop/Pi colors `5557`, camera settings `5559`; multipart `[JPEG, JSON]` video/status compatibility; unknown config keys preserved on write.
- Runtime calibration files are local non-git seed data, not GitHub artifacts: `saved_gates.json`, `camera_settings.json`, `field_map.json`, `distance_calib.json`, threshold/calibration data. Missing files must yield defaults with warnings.
- Pi deployment is temporary smoke only: build, upload/run, verify, stop service, do not overwrite calibration/config unless test requires it.
- Mega firmware flash is allowed only after build/protocol parity/logs/timeouts/rollback are explicitly recorded; if unclear, stop and document.
- INFO text describing application functions is Russian; implementation/comments/docs/commits in English.
- No passwords, keys, tokens, host passwords, or private credentials in git, logs, plan, or final report.
- Every task ends with a reviewable artifact and green targeted tests. Do not mark a task done on unverified hardware claims.
- Laptop sleep is the final action after green verification or documented blockers, never before the final report.

## Review Focus

1. **Corrupt/missing runtime config** -> config loader must fall back to defaults and warn, never crash; pinned in Task 3.
2. **Stale Pi gates / missing `enabled`** -> radar/status must not render unconfigured gates even when stale saved state exists; pinned in Tasks 9 and 14.
3. **Full-FOV/no-AE camera contract** -> capture must open `/dev/video0|1` or Windows MSMF camera at 1920x1080 and camera settings must never re-enable AE; pinned in Tasks 10 and 11.
4. **HSV mask alignment/no warp** -> masks align 1:1 with the RGB portrait frame; window resizes keep aspect with letterbox; pinned in Task 14.
5. **Pi offline / service failed / no ack** -> CAM SET must show truthful state rather than falsely "applied"; pinned in Task 11 and Task 20.

---

## File Structure

```text
C:\Users\sweetener\Desktop\camera_cpp
  CMakeLists.txt
  CMakePresets.json
  cmake/
    Dependencies.cmake
    Warnings.cmake
    TargetsMacros.cmake
  .gitignore
  README.md
  src/
    common/
      atomic_file.hpp/.cpp
      json_config.hpp/.cpp
      logging.hpp/.cpp
      paths.hpp/.cpp
      ids.hpp/.cpp
      zmq_framing.hpp/.cpp
      contracts.hpp
    vision/
      hsv_color.hpp/.cpp
      gate_cluster.hpp/.cpp
      self_mask.hpp/.cpp
      distance.hpp/.cpp
      tracking.hpp/.cpp
      calibration.hpp/.cpp
    robot_core/
      angles.* ball_logic.* gate_status.* heading_ctrl.* line_logic.*
      link_codec.* omni.* tactics.* drive_limits.*
    pi_camera/
      v4l2_capture.hpp/.cpp
      camera_engine.hpp/.cpp
      track_service.hpp/.cpp
      cam_settings.hpp/.cpp
      status_builder.hpp/.cpp
      service_main.cpp
    laptop/
      main.cpp
      app.hpp/.cpp
      state.hpp/.cpp
      source_controller.hpp/.cpp
      widgets.hpp/.cpp
      panels.hpp/.cpp
      help_ru.hpp/.cpp
      radar_panel.hpp/.cpp
      hsv_panel.hpp/.cpp
      cam_settings_panel.hpp/.cpp
      theme.hpp/.cpp
    mega_fw/
      platformio.ini
      src/
      tests/
  tests/
    common/
    vision/
    parity/
      fixtures/
      parity_runner.*
    robot_core/
    integration/
  assets/fixtures/
    scene_red_folder.png
    saved_gates.example.json
  docs/
    BUILD.md
    DEPLOY.md
    VERIFICATION.md
    FIDELITY.md
  third_party/
    imgui/          # only if vendored strategy selected
  local/            # gitignored runtime seed data
```

Dependency strategy: prefer `find_package` for OpenCV/ZMQ/SDL2; use vcpkg manifest fallback for Windows if discovery fails; vendor Dear ImGui if vcpkg/source package unavailable; Pi uses system OpenCV/ZMQ and no SDL.

---

### Task 1: Scaffold new repository and CMake skeleton

**Files:**
- Create: `C:\Users\sweetener\Desktop\camera_cpp\CMakeLists.txt`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\CMakePresets.json`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\cmake\Dependencies.cmake`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\cmake\Warnings.cmake`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\.gitignore`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\README.md`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\src\common\.gitkeep`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\tests\.gitkeep`

**Interfaces:**
- Produces: a configureable CMake workspace before any product code exists. Target `kCameraCppSmoke` compiles a trivial `src/common/smoke.cpp` returning 0.

- [ ] **Step 1: Initialize repository**

Run: `git init -b native-optimization` in `C:\Users\sweetener\Desktop\camera_cpp`; record `git status --short`.

- [ ] **Step 2: Write `.gitignore`**

Ignore: `build*/`, `local/`, `out/`, `*.user`, IDE caches, `.vscode/`, `.pio/`, calibration runtime files (`saved_gates.json`, `camera_settings.json`, `field_map.json`, `distance_calib.json`), credentials/keys.

- [ ] **Step 3: Write CMake skeleton**

`CMakePresets.json` exposes `windows-debug`, `windows-release`, `pi-release`. Root `CMakeLists.txt` sets C++17, warnings module, `enable_testing()`, `add_subdirectory(src)` and includes `cmake/Dependencies.cmake`.

- [ ] **Step 4: Add dependency discovery module**

`Dependencies.cmake` tries `find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)` and `find_package(ZMQ REQUIRED)` or `find_library`; documents Windows vcpkg fallback. SDL2/Dear ImGui are not required for common libraries yet.

- [ ] **Step 5: Add smoke binary and configure test**

Create `src/common/smoke.cpp` with `int main(){return 0;}` and `tests/common/smoke_test.cpp` using EXIT_SUCCESS. Configure with `"C:\Program Files\CMake\bin\cmake.exe" -S . -B build -G "Ninja"` if Ninja found, else default generator. Expected: configure succeeds or Ninja missing is documented and default generator succeeds.

- [ ] **Step 6: Commit**

```bash
git add .
git commit -m "chore: scaffold native project"
```

---

### Task 2: Seed local runtime data and spec/plan copies without secrets

**Files:**
- Create: `C:\Users\sweetener\Desktop\camera_cpp\local\README.md`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\local\saved_gates.json`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\local\camera_settings.json`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\local\field_map.json`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\local\distance_calib.json`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\docs\spec.md`
- Create: `C:\Users\sweetener\Desktop\camera_cpp\docs\implementation_plan.md`

**Interfaces:**
- Produces: gitignored local seed directory. Runtime code reads `local/` first, then defaults; git never contains these files.

- [ ] **Step 1: Copy spec and plan into `docs/spec.md` and `docs/implementation_plan.md`**

Use the approved design text and this plan, unchanged except path references updated to `camera_cpp` where needed.

- [ ] **Step 2: Copy runtime contract files into `local/`**

Copy from `C:\Users\sweetener\Desktop\camera\UniKostyl\saved_gates.json`, `camera_settings.json`, `field_map.json`, `distance_calib.json` if present. If absent, write an empty valid placeholder using the old contracts (`{"version":1,"gates":{}}`).

- [ ] **Step 3: Verify ignored**

Run `git status --short --ignored local`; expected: `local/` entries shown as `!!`, not staged.

- [ ] **Step 4: Commit only docs, not local data**

```bash
git add docs/spec.md docs/implementation_plan.md
git commit -m "docs: carry native design and plan"
```

---

### Task 3: Atomic config and JSON foundation with corrupt-file tests

**Files:**
- Create: `src/common/atomic_file.hpp`, `src/common/atomic_file.cpp`
- Create: `src/common/json_config.hpp`, `src/common/json_config.cpp`
- Create: `src/common/paths.hpp`, `src/common/paths.cpp`
- Create: `src/common/logging.hpp`, `src/common/logging.cpp`
- Create: `src/common/contracts.hpp`
- Test: `tests/common/atomic_file_test.cpp`
- Test: `tests/common/json_config_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `bool atomic_write_text(const std::filesystem::path& path, std::string_view text, std::string* error);`
  - `std::optional<nlohmann::json> read_json_file(const std::filesystem::path& path, std::string* warning);`
  - `std::filesystem::path contract_path(const std::string& name);` resolving `local/` first, then repo-local default path.
  - `enum class GateId { One=1, Two=2, Three=3 };` and shared constants `ports::kVideoStatus=5555`, `ports::kColors=5557`, `ports::kCamSettings=5559`.

- [ ] **Step 1: Write failing atomic file tests**

Cases: same-dir temp file renamed into place; unknown JSON keys survive rewrite; corrupt JSON returns `std::nullopt` + warning; concurrent-looking stale temp cleanup; missing parent directory returns false with error.

- [ ] **Step 2: Run tests and verify failure**

Run `ctest -R atomic_file --output-on-failure`; expected FAIL because functions do not exist.

- [ ] **Step 3: Implement atomic writer**

Use `.tmp.<pid>.<counter>` in same directory, `std::ofstream`, `flush`, fsync where available, then `std::filesystem::rename`/`_wrename` equivalent. Preserve permissions best-effort. On failure return false; never delete original.

- [ ] **Step 4: Implement JSON config helpers**

`read_json_file`: valid JSON -> object; invalid/empty/missing -> `std::nullopt` and non-throwing warning. Unknown keys are never dropped by helper rewrite functions; only explicit typed upserts may modify owned keys.

- [ ] **Step 5: Add contracts header**

Defines frame size constants (`kPiFrameW=640`, `kPiFrameH=360`, `kLaptopPortraitW=720`, `kLaptopPortraitH=1280`), port constants, gate id helpers, and status JSON key names as `constexpr std::string_view`.

- [ ] **Step 6: Run tests**

Expected: atomic/config tests PASS; smoke test PASS.

- [ ] **Step 7: Commit**

```bash
git add src/common tests/common CMakeLists.txt
git commit -m "feat(common): add atomic config foundation"
```

---

### Task 4: Vendor Dear ImGui/SDL2 discovery and laptop dependency gate

**Files:**
- Create: `cmake/LaptopDependencies.cmake`
- Create: `third_party/THIRD_PARTY.md`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`
- Test: `tests/laptop/dependency_gate_test.cpp`

**Interfaces:**
- Produces: `CAMERA_CPP_HAS_LAPTOP_DEPS` CMake option and compile definition; laptop target links only when true; if deps missing, laptop target is skipped with explicit message but common/vision/pi targets still build.

- [ ] **Step 1: Choose dependency route in writing, not code**

Document in `third_party/THIRD_PARTY.md`: Windows uses CMake find_package first; if missing, vcpkg manifest `vcpkg.json` pinned to `sdl2`, `fmt`, `imgui[sdl2-binding,opengl3-binding]`; fallback vendor Dear ImGui + SDL2 binaries only after review, never commit binaries without license/version note.

- [ ] **Step 2: Write a configure-time dependency probe test**

Probe `find_package(SDL2 QUIET)`, `find_package(OpenGL QUIET)`, `imgui` presence by target `imgui::imgui` or known fallback target. On missing deps print `"[laptop] deps missing; laptop target skipped (see third_party/THIRD_PARTY.md)"` and set `CAMERA_CPP_HAS_LAPTOP_DEPS=OFF`.

- [ ] **Step 3: Configure with dummy missing deps path**

Expected: configure succeeds on Pi/common-only; Windows with deps absent still allows `pi_camera` and `common` configure.

- [ ] **Step 4: Document exact Windows dependency install commands**

In `THIRD_PARTY.md`: `vcpkg install sdl2 imgui[sdl2-binding,opengl3-binding] --triplet x64-windows` and CMake configure examples with `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.

- [ ] **Step 5: Commit**

```bash
git add cmake/LaptopDependencies.cmake third_party/THIRD_PARTY.md CMakeLists.txt CMakePresets.json tests/laptop
git commit -m "build: gate laptop UI dependencies"
```

---

### Task 5: HSV palette and color-conversion core with Python parity vectors

**Files:**
- Create: `src/vision/hsv_color.hpp`, `src/vision/hsv_color.cpp`
- Test: `tests/vision/hsv_color_test.cpp`
- Test: `tests/parity/hsv_vectors.h` (generated or copied deliberately)
- Create: `tests/parity/parity_runner.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct Lab { float L, A, B; };`
  - `RgbLab rgb_to_lab_u8(uint8_t r, uint8_t g, uint8_t b);`
  - `RgbLab lab_to_rgb_u8(float l, float a, float b);`
  - `bool lab_in_range(const rgb_lab& c, const lab_range& r);`
  - `void rgb_to_opencv_range(...)`-equivalent for thresholds used by old reference; exact formulas copied from reference before porting, verified with vectors.

- [ ] **Step 1: Write failing tests for known colors**

Black/white/green/red/yellow/blue/orange LAB vectors from Python reference. Tolerance +/-1 for 8-bit conversion; exact parity test for `lab_in_range`.

- [ ] **Step 2: Inspect reference formulas**

Read `C:\Users\sweetener\Desktop\camera\UniKostyl\unikostyl.py` sections `rgb2lab`, `lab_to_rgb`, `threshold_filter`, and tests in `out/mask_check.py`; record any non-obvious constant in comments.

- [ ] **Step 3: Implement minimal conversion and range test**

Do not optimize until exact tests pass; after pass, remove temporary allocations.

- [ ] **Step 4: Add parity runner**

`parity_runner` reads a simple text vector file (`input_rgb L A B out_ok`) and asserts exact range behavior. Keep vector generator as documented Python one-liner, not a runtime dependency.

- [ ] **Step 5: Run all vision tests**

Expected: HSV/LAB tests PASS; no OpenCV dependency required for pure conversion tests.

- [ ] **Step 6: Commit**

```bash
git add src/vision/hsv_color* tests/vision/hsv_color_test.cpp tests/parity
git commit -m "feat(vision): port color conversion core"
```

---

### Task 6: Gate clustering and false-positive shape guard in native C++

**Files:**
- Create: `src/vision/gate_cluster.hpp`, `src/vision/gate_cluster.cpp`
- Test: `tests/vision/gate_cluster_test.cpp`
- Test: `tests/parity/cluster_parity.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct GateComponent { int x,y,w,h,area; float cx,cy; };`
  - `struct GateClusterParams { float merge_gap_frac, max_stray_frac; int open_kernel, lost_frames, confirm_frames, max_candidates; float max_aspect, min_cluster_side_frac; };`
  - `GateObject build_gate_object(const std::vector<GateComponent>&, const GateClusterParams&, std::optional<float> prev_x, std::optional<float> prev_y);`
  - `struct GateObject { bool found=false; float cx=0, cy=0, radius=0, area=0; Rect bbox; int members=0; };`
  - `GateStability` class with confirm/lost hysteresis matching reference behavior.

- [ ] **Step 1: Port false-gate tests first**

Cases: red folder cluster survives; long lamp cluster rejected; long tape strip rejected; half-gate at frame edge survives; split goal posts merge across 13 px gap; empty mask returns not found.

- [ ] **Step 2: Port candidate-cap and merge-gap tests**

Expected 256 candidate cap behavior from reference `cluster_check.py`; merged pieces returned; strays rejected.

- [ ] **Step 3: Implement shape guard exactly as approved**

After component merge and before anchor selection: reject aggregate if `min(width,height) < min_cluster_side_px` or `max(width,height)/min(width,height) > max_aspect`. Defaults: `max_aspect=6.5`, `min_cluster_side_frac=0.008` using smaller frame dimension.

- [ ] **Step 4: Add Python/C++ parity harness**

Reuse old `UniKostyl/out/parity_check5.py` expectations, but call native binary `pi_core/tests`-style parity mode or direct C++ test vectors. Compare Python output vs C++ output on at least 61 deterministic frames. If Python reference import is unavailable in CI, store vectors generated from the reference implementation in `tests/parity/fixtures/cluster_vectors.txt`.

- [ ] **Step 5: Run tests**

Expected: native cluster tests PASS; parity report says OK or lists exact mismatch counts; false-gate regression PASS.

- [ ] **Step 6: Commit**

```bash
git add src/vision/gate_cluster* tests/vision/gate_cluster_test.cpp tests/parity
git commit -m "feat(vision): port robust gate clustering"
```

---

### Task 7: Self-mask and distance estimation native port

**Files:**
- Create: `src/vision/self_mask.hpp`, `src/vision/self_mask.cpp`
- Create: `src/vision/distance.hpp`, `src/vision/distance.cpp`
- Test: `tests/vision/self_mask_test.cpp`
- Test: `tests/vision/distance_test.cpp`

**Interfaces:**
- Produces:
  - `struct SelfMask { bool enabled=false; float cx=0.5, cy=0.5, r=0.22, r2=0.0; };`
  - `cv::Mat build_keep_mask(cv::Size size, const SelfMask&);`
  - `struct DistModel { float d=0, a=0, b=0, c=0; bool reliable=false; };`
  - `DistanceEstimator` with `update(pixel_radius, frame_width, gate_width_cm) -> DistResult`, including occlusion hold behavior.

- [ ] **Step 1: Port self-mask keep tests**

Center-circle mask zeros pixels inside robot body; outside survives; disabled mask returns all-ones; malformed params clamp to defaults.

- [ ] **Step 2: Port distance tests**

Radius-to-distance model tests from `distance_check.py`: monotonicity, minimum clamp, occlusion hold, invalid gate size handling, status JSON additive fields if added later.

- [ ] **Step 3: Implement keep mask with OpenCV primitives**

Use `cv::circle`, `cv::rectangle`, or bitwise inverse; no Python per-pixel loops. Reuse output buffer where caller supplies it.

- [ ] **Step 4: Implement distance estimator**

Closed-form approximation only after reading the reference formulas; no invented physical constants beyond copied reference defaults.

- [ ] **Step 5: Run tests**

Expected: self-mask and distance PASS; parity vector file added for distance if reference vectors exist.

- [ ] **Step 6: Commit**

```bash
git add src/vision/self_mask* src/vision/distance* tests/vision
git commit -m "feat(vision): port self-mask and distance models"
```

---

### Task 8: Tracking pipeline with mask export and cached buffers

**Files:**
- Create: `src/vision/tracking.hpp`, `src/vision/tracking.cpp`
- Create: `src/vision/calibration.hpp`, `src/vision/calibration.cpp`
- Test: `tests/vision/tracking_test.cpp`
- Test: `tests/vision/fixtures/tracking_scene.png`

**Interfaces:**
- Produces:
  - `struct GateThreshold { int l_min,l_max,a_min,a_max,b_min,b_max; bool configured=false; };`
  - `struct TrackInput { const cv::Mat* portrait_rgb; std::array<GateThreshold,3> thresholds; SelfMask self_mask; std::array<std::optional<cv::Point2f>,3> previous_positions; };`
  - `struct GateTrackResult { bool enabled=false, found=false, held=false; float angle_deg=0, radius_px=0, dist_cm=0; bool dist_reliable=false; cv::Rect bbox; cv::Mat mask; };`
  - `std::array<GateTrackResult,3> track_gates(TrackInput in);` reusing internal buffers via a `TrackingEngine` class.

- [ ] **Step 1: Write failing tests for zero configured gates**

All thresholds defaulted/unconfigured -> three results disabled/not found; no masks exported; no crash on null frame.

- [ ] **Step 2: Write failing tests for one configured gate**

Synthetic scene with a bright gate-colored rectangle yields `enabled=true`, `found=true`; mask has same shape as input; angle convention matches reference (0 degrees right, CCW); bbox centered on fixture.

- [ ] **Step 3: Port LAB mask path**

Convert RGB->LAB, threshold three channels with `cv::inRange`/`bitwise_and`, apply self-mask, run connected components, clustering from Task 6. Reuse Mats as members of `TrackingEngine`.

- [ ] **Step 4: Add stage timings**

`TrackingEngine::last_stage_ms()` returns approximate `cvt_ms`, `mask_ms`, `cc_ms`, `cluster_ms`, `total_ms`. Tests assert fields are populated and non-negative.

- [ ] **Step 5: Verify memory behavior**

Run test with `CV_TRACE` or manual counter; assert no obvious reallocation growth across 100 frames by checking engine buffer sizes stable.

- [ ] **Step 6: Commit**

```bash
git add src/vision/tracking* src/vision/calibration* tests/vision/tracking_test.cpp
git commit -m "feat(vision): add native gate tracking pipeline"
```

---

### Task 9: Native persistence for gates, camera settings, field map

**Files:**
- Create: `src/common/gate_store.hpp`, `src/common/gate_store.cpp`
- Create: `src/common/field_map.hpp`, `src/common/field_map.cpp`
- Create: `src/common/cam_settings_config.hpp`, `src/common/cam_settings_config.cpp`
- Test: `tests/common/gate_store_test.cpp`
- Test: `tests/common/field_map_test.cpp`
- Test: `tests/common/cam_settings_config_test.cpp`

**Interfaces:**
- Produces:
  - `GateStore::load(path)`, `GateStore::upsert(gate_id, GateColor, Role)`, `GateStore::roles()`, `GateStore::save_atomic()`; unknown keys preserved.
  - `FieldMap::snapshot_from_results(...)`, `estimated_directions(...)`, `save_atomic()`; binding with no measured gates returns `{ok=false, warning="no measured gates"}`.
  - `CamSettingsConfig::load()`, `save()` with control ranges table shared with Pi and laptop code.

- [ ] **Step 1: Port role and persistence tests**

Cover unset/ours/opponent, missing `enabled` defaults false, stale Pi gate records not restored as configured, corrupt file falls back to defaults, unknown keys preserved.

- [ ] **Step 2: Port range clamp tests**

Camera controls: exposure 1..5000, gain 0..100, brightness -64..64, gamma 72..500, saturation 0..128, contrast 0..64, power_line_frequency 0..2, exposure_dynamic_framerate 0..1.

- [ ] **Step 3: Implement field map binding**

`bind_from_results` chooses fresh Pi status when available/fresh, else current local results; refuses when no enabled+found gates; writes atomically preserving boundary section and unknown sections.

- [ ] **Step 4: Run tests**

Expected: all persistence tests PASS; no git tracked runtime files are created.

- [ ] **Step 5: Commit**

```bash
git add src/common tests/common
git commit -m "feat(common): port persistent configuration stores"
```

---

### Task 10: V4L2 camera capture and full-FOV service core

**Files:**
- Create: `src/pi_camera/v4l2_capture.hpp`, `src/pi_camera/v4l2_capture.cpp`
- Create: `src/pi_camera/camera_engine.hpp`, `src/pi_camera/camera_engine.cpp`
- Test: `tests/pi_camera/capture_config_test.cpp`
- Test: `tests/pi_camera/fixtures/test_pattern_1920x1080.jpg`

**Interfaces:**
- Produces:
  - `struct CaptureConfig { int width=1920, height=1080, fps=30; std::string fourcc="MJPG"; std::string device="/dev/video0"; };`
  - `class V4l2Capture { bool open(); std::optional<cv::Mat> read(); void close(); };`
  - `cv::Mat downscale_and_rotate(const cv::Mat& bgr_1920x1080);` returns `640x360` Pi processing frame.

- [ ] **Step 1: Write config-validation tests**

Invalid FPS/resolution/FourCC are rejected; non-MJPG selection warns but may proceed if explicit; device path empty yields default `/dev/video0`; missing V4L2 backend reports truthful error.

- [ ] **Step 2: Implement capture path with OpenCV VideoCapture + V4L2 backend**

Set `CAP_PROP_FOURCC=MJPG`, `CAP_PROP_FRAME_WIDTH=1920`, `CAP_PROP_FRAME_HEIGHT=1080`, `CAP_PROP_FPS=30`; verify negotiated size/FOURCC; do not request AE changes.

- [ ] **Step 3: Implement software downscale**

Decode high-quality MJPG, use `INTER_LINEAR` or tested equivalent; document measured choice; never crop sensor.

- [ ] **Step 4: Add mocked capture test if V4L2 unavailable**

Inject `CaptureBackend` interface; test pipeline on fixture image without camera.

- [ ] **Step 5: Commit**

```bash
git add src/pi_camera tests/pi_camera
git commit -m "feat(pi): add full-fov capture engine"
```

---

### Task 11: Pi CAM SET live controls, status JSON, and color listener

**Files:**
- Create: `src/pi_camera/cam_settings.hpp`, `src/pi_camera/cam_settings.cpp`
- Create: `src/pi_camera/status_builder.hpp`, `src/pi_camera/status_builder.cpp`
- Create: `src/pi_camera/color_listener.hpp`, `src/pi_camera/color_listener.cpp`
- Create: `src/pi_camera/service_main.cpp`
- Test: `tests/pi_camera/status_builder_test.cpp`
- Test: `tests/pi_camera/cam_settings_test.cpp`

**Interfaces:**
- Produces:
  - `CamSettings::apply_controls(device, controls, last_known) -> ApplyReport`; controls are UVC units; AE locked to manual (`auto_exposure=1`).
  - `StatusBuilder::build(gates, fps, frame_size, source) -> nlohmann::json`; includes gate records with `enabled`, `found`, `angle`, `area`, `fps`.
  - `ColorListener` receives multipart `[JSON]` or compat list on port 5557; parses roles + RGB; updates tracker thresholds via shared `GateStore`.
  - `CamSettingsListener` CONFLATE SUB on port 5559; full set reapplied; no acks invented.

- [ ] **Step 1: Write status JSON tests**

Multipart shape is `[JPEG, JSON]`; JSON has FPS, gates array, source/status fields; missing `enabled` is false; `found` without enabled is normalized by laptop consumer and Pi builder.

- [ ] **Step 2: Write camera-apply tests with mocked backend**

Clamping works; unknown control rejected with `n/a`; AE is forced manual; readback mismatch reported truthfully; no silent "ok".

- [ ] **Step 3: Implement ZMQ listeners**

Color SUB `tcp://*:5557`; settings CONFLATE SUB `tcp://*:5559`; PUB video/status `tcp://*:5555`; loop timeouts bounded; shutdown flag joins threads cleanly.

- [ ] **Step 4: Add CLI contract**

`camera_service --laptop-ip 10.132.122.120 --no-serial --device /dev/video0 --port-video 5555` with `--self-mask-*` passthrough and `--help`; invalid args exit 2 with usage.

- [ ] **Step 5: Commit**

```bash
git add src/pi_camera tests/pi_camera
git commit -m "feat(pi): add live settings and status service"
```

---

### Task 12: Pi native service end-to-end Linux build/test

**Files:**
- Create: `docs/DEPLOY.md`
- Create: `scripts/deploy_pi_temp.sh`
- Create: `scripts/stop_pi_temp.sh`
- Test: `tests/integration/pi_service_smoke.py`

**Interfaces:**
- Produces: deploy commands and smoke runner for temporary Pi run; no permanent service enablement.

- [ ] **Step 1: Write deploy doc with safety commands**

Upload binary/modules to `/tmp/camera_cpp_smoke`; run with `--no-serial`; verify `ss -ltnp | grep 5555`; capture multipart JPEG/JSON for 5 seconds; stop process by PID/pattern; confirm port closed; restore any touched config from backup.

- [ ] **Step 2: Write smoke script**

`pi_service_smoke.py` connects to `tcp://10.132.122.51:5555`, reads multipart for up to 10 s, asserts JPEG SOI/EOI bytes and parses JSON status; prints JSON summary.

- [ ] **Step 3: Build on Pi**

Command: `ssh twox@10.132.122.51 "cd /tmp/camera_cpp_smoke && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4"` after syncing sources. Expected: build succeeds.

- [ ] **Step 4: Run temporary smoke**

Expected: 5555 opens, frames parse, status JSON valid, CAM SET can be sent to 5559 and logged/applied, process stops, 5555 closes, config checksum unchanged unless test opted in.

- [ ] **Step 5: Record result in docs/VERIFICATION.md**

Include exact command and unverified items.

- [ ] **Step 6: Commit**

```bash
git add docs/DEPLOY.md scripts tests/integration
git commit -m "docs(pi): add temporary native smoke procedure"
```

---

### Task 13: SDL2/Dear ImGui laptop shell, CONNECT, clipboard paste

**Files:**
- Create: `src/laptop/main.cpp`
- Create: `src/laptop/app.hpp`, `src/laptop/app.cpp`
- Create: `src/laptop/state.hpp`, `src/laptop/state.cpp`
- Create: `src/laptop/source_controller.hpp`, `src/laptop/source_controller.cpp`
- Create: `src/laptop/widgets.hpp`, `src/laptop/widgets.cpp`
- Test: `tests/laptop/clipboard_test.cpp`
- Test: `tests/laptop/source_controller_test.cpp`

**Interfaces:**
- Produces:
  - `struct AppState { std::string camera_source_text="10.132.122.51"; bool input_active=false; enum class SourceKind { Local, Pi, None } source_kind=SourceKind::None; std::string pi_status_summary; double fps_ui=0; };`
  - `std::string paste_text_from_clipboard();` using SDL2 clipboard; strips NUL/control/non-printable except newline->trimmed; caps 256 chars.
  - `ConnectRequest parse_connect(text)` supporting `local`, integer camera index, IPv4/hostname:port optional; invalid text yields inline error, not crash.

- [ ] **Step 1: Write clipboard tests**

UTF-8 text preserved; UTF-16 mishandled packets decoded if SDL returns broad bytes; control chars stripped; empty clipboard returns empty string.

- [ ] **Step 2: Write source parser tests**

`"10.132.122.51"` selects Pi source; `"local"` selects local ELP index autodetect; `"1"` selects local index 1; empty/invalid rejected with error.

- [ ] **Step 3: Implement SDL window loop skeleton**

1280x720 default resizable dark theme; event loop handles `SDL_QUIT`, `SDL_WINDOWEVENT_RESIZED`, keyboard input; ImGui initialized only if laptop deps present.

- [ ] **Step 4: Extract pure `parse_connect`**

No SDL dependency in parser unit tests.

- [ ] **Step 5: Commit**

```bash
git add src/laptop tests/laptop
git commit -m "feat(laptop): add native shell and paste handling"
```

---

### Task 14: Laptop UI panels and live controls parity

**Files:**
- Create: `src/laptop/panels.hpp`, `src/laptop/panels.cpp`
- Create: `src/laptop/cam_settings_panel.hpp`, `src/laptop/cam_settings_panel.cpp`
- Create: `src/laptop/radar_panel.hpp`, `src/laptop/radar_panel.cpp`
- Create: `src/laptop/hsv_panel.hpp`, `src/laptop/hsv_panel.cpp`
- Create: `src/laptop/help_ru.hpp`, `src/laptop/help_ru.cpp`
- Create: `src/laptop/theme.hpp`, `src/laptop/theme.cpp`
- Modify: `src/laptop/app.hpp`, `src/laptop/app.cpp`
- Test: `tests/laptop/help_ru_test.cpp`
- Test: `tests/laptop/hsv_panel_test.cpp`
- Test: `tests/laptop/layout_smoke_test.cpp`

**Interfaces:**
- Produces:
  - `PanelTheme` palette constants matching approved dark UI.
  - `draw_source_panel`, `draw_video_panels`, `draw_gates_panel`, `draw_cam_settings`, `draw_radar`, `draw_hsv_mask`, `draw_help`.
  - `HelpRu::sections()` returns Russian strings for `main`, `gates`, `radar`, `camera`, `hsv`, `field`, `status`; every reference control name appears.

- [ ] **Step 1: Write Russian INFO content test**

Asserts required strings: `Ctrl+V`, `CAM SET`, `RADAR`, `BIND FIELD`, `POS`, `HSV MASK`, `INFO`, `SAVE G1`, `GATES`, `exposure`, `brightness`.

- [ ] **Step 2: Implement HSV panel builder pure function**

`build_hsv_mask_bgra(frame_rgb, masks, colors, palette)` returns BGRA image same shape; letterboxing never changes pixels; mask tint edge alignment tests pass.

- [ ] **Step 3: Implement layout with immediate-mode UI**

Top row: R mode, zoom +/-, Bitmap/L/A/B, pause, CAM SET, RADAR, BIND FIELD, POS, HSV MASK, INFO. Right column: camera source/connect/status; left: video + mask; bottom: sliders and GATES/SAVE.

- [ ] **Step 4: Wire widgets to state**

Clicking CONNECT sets source; Ctrl+V pastes; INFO opens modal; HSV toggles aligned mask; RADAR toggles panel; SAVE sends color message; CAM SET applies local or Pi path with truthful status.

- [ ] **Step 5: Test screenshot smoke on SDL dummy driver if available**

Capture at least one 1280x720 PNG; verify non-black regions and no exceptions. Real live smoke remains Task 19/20.

- [ ] **Step 6: Commit**

```bash
git add src/laptop tests/laptop
git commit -m "feat(laptop): add native UI panels and controls"
```

---

### Task 15: Robot core native port and protocol parity

**Files:**
- Create: `src/robot_core/*`
- Create: `tests/robot_core/*`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `robot_core` library with ball/line/tactics/heading/omni logic and `link_codec` compatible with Mega firmware protocol v1; no Arduino dependency.

- [ ] **Step 1: Port angles/heading tests**

Heading wrap-around, deadband, PI correction output clamp, omni vector projection, drive limit arbitration.

- [ ] **Step 2: Port ball/line logic tests**

Ball visible/lost, line detected states, tactics FSM transitions, sensor timeout failsafe.

- [ ] **Step 3: Port link codec tests**

Binary frame encode/decode, CRC8 check, malformed frame rejection, CRC mismatch rejection, sequence/ack behavior per old firmware tests.

- [ ] **Step 4: Build native core on Windows and Pi**

Expected: both build; tests pass on both.

- [ ] **Step 5: Commit**

```bash
git add src/robot_core tests/robot_core CMakeLists.txt
git commit -m "feat(robot): port robot control core"
```

---

### Task 16: Mega firmware port, build, and protocol parity

**Files:**
- Create: `src/mega_fw/platformio.ini`
- Create: `src/mega_fw/src/*`
- Create: `src/mega_fw/tests/*`
- Create: `docs/FIRMWARE.md`

**Interfaces:**
- Produces: PlatformIO project or equivalent firmware build path; protocol compatible with `link_codec` from Task 15.

- [ ] **Step 1: Port firmware skeleton without flashing**

Safety/watchdog first: motors disabled on boot unless valid frame; DMP/drift-freeze behavior represented as stubbed or actual per platform support; raw serial lines preserved behind compile flag.

- [ ] **Step 2: Build firmware binary**

Run PlatformIO build if installed; otherwise document missing dependency and provide exact install command; compile must pass before any flash.

- [ ] **Step 3: Run host-side protocol test**

Extract CRC/frame logic into host-testable functions; assert Pi codec and Mega codec agree byte-for-byte on golden vectors.

- [ ] **Step 4: Flash gate documentation**

`docs/FIRMWARE.md` contains exact command, timeout, expected serial log, recovery steps (power-cycle, reflash known-good hex), and explicit statement that flashing is only performed after approval in this session.

- [ ] **Step 5: Commit**

```bash
git add src/mega_fw docs/FIRMWARE.md
git commit -m "feat(firmware): add protocol-compatible mega core"
```

---

### Task 17: Windows laptop release build and parity report

**Files:**
- Create: `docs/VERIFICATION.md`
- Create: `scripts/verify_windows.ps1`
- Modify: `CMakePresets.json`

**Interfaces:**
- Produces: documented Windows build/test commands and recorded results.

- [ ] **Step 1: Configure and build release**

Use WinLibs GCC on PATH and `"C:\Program Files\CMake\bin\cmake.exe"`. Expected: common/vision/robot_core/pi_camera build; laptop builds if deps installed; if laptop deps missing, report blocker explicitly.

- [ ] **Step 2: Run full unit test suite**

Expected output includes all PASS; any SKIP must name missing dependency; no silent pass.

- [ ] **Step 3: Run parity checks**

Run `ctest -N` and parity target if configured; report Python/C++ cluster/HSV/distance exact match counts.

- [ ] **Step 4: Record metrics**

Use fixture frames; report FPS estimate for synthetic pipeline and real local camera only in Task 19. Store raw logs under `build/reports` and summarize in `docs/VERIFICATION.md`.

---

### Task 18: Pi build and native service verification

**Files:**
- Create: `scripts/verify_pi.sh`
- Modify: `docs/VERIFICATION.md`

**Interfaces:**
- Produces: reproducible Pi build/test commands and logs stored locally.

- [ ] **Step 1: Sync sources to Pi temporary directory**

Use `scp`/`rsync` to `/tmp/camera_cpp_verify`; never overwrite `/home/twox` files.

- [ ] **Step 2: Configure/build with system OpenCV/ZMQ**

Expected: OpenCV 4.10 found; ZMQ 26.4 found; `pi_camera` service builds.

- [ ] **Step 3: Run unit/parity tests on Pi**

Expected: all tests pass; camera smoke is Task 20.

- [ ] **Step 4: Update verification doc**

Include Pi compiler versions, warnings, skipped items.

---

### Task 19: Local ELP live smoke on laptop

**Files:**
- Create: `scripts/live_smoke_local.ps1`
- Modify: `docs/VERIFICATION.md`

**Interfaces:**
- Produces: bounded 45-60 second local camera smoke with screenshot and logs.

- [ ] **Step 1: Launch app with local source auto-detect**

Expected: camera index found; 1920x1080 opens; FPS >0; UI shows local source; no crash.

- [ ] **Step 2: Capture screenshot and log**

Save `build/reports/local_live_smoke.png` and log; confirm no top-row overlap and HSV MASK/INFO buttons visible.

- [ ] **Step 3: Exercise safe controls only**

Toggle HSV MASK/INFO; verify windows/menus open/close; do not change physical robot hardware.

- [ ] **Step 4: Record result**

If not verified, mark FAILED/BLOCKED in `VERIFICATION.md`, not "done".

---

### Task 20: Temporary Pi native live smoke

**Files:**
- Modify: `docs/VERIFICATION.md`
- Modify: `scripts/deploy_pi_temp.sh`

**Interfaces:**
- Produces: temporary native service run compared against old Python reference stream.

- [ ] **Step 1: Stop or bypass old Python service safely**

Identify current `rpi_elp_cam.py` PID; do not kill unless replacing for test; if replacing, record PID and restore/restart afterward if needed. Prefer alternate ports (`5556`/`5560`) for comparison before replacing 5555.

- [ ] **Step 2: Run native service temporarily on alternate ports**

`--port-video 5556 --port-colors 5558 --cam-settings-port 5560 --no-serial` first; verify frames/status without disturbing current Pi service.

- [ ] **Step 3: CAM SET probe**

Send a harmless but observable CAM SET (e.g. gamma small step or brightness readback if safe); verify Pi logs apply or truthfully reports rejection; restore prior settings from backup afterward.

- [ ] **Step 4: Stop native service and confirm closure**

Ports closed; original service state restored or documented.

- [ ] **Step 5: Update verification report**

Do not claim hardware green if any step failed.

---

### Task 21: Final metrics, report, and sleep

**Files:**
- Modify: `docs/VERIFICATION.md`
- Create: `docs/FINAL_REPORT.md`
- Modify: `README.md`

**Interfaces:**
- Produces: final technical report and normal Windows sleep command.

- [ ] **Step 1: Compile metrics**

Include Windows build result, Pi build result, unit test counts, parity counts, local smoke FPS, Pi smoke FPS/CPU if available, unresolved blockers.

- [ ] **Step 2: Verify repo state**

`git status --short`; intended files committed; `local/` ignored; no credentials staged; original camera folder untouched except approved docs.

- [ ] **Step 3: Write final report**

Sections: What was built, what is verified, what is unverified, exact Pi/camera state left behind, rollback/recovery notes, next human actions.

- [ ] **Step 4: Sleep laptop**

Run `rundll32.exe powrprof.dll,SetSuspendState 0,1,0` or equivalent documented normal sleep command only after report is saved. Do not hibernate.

