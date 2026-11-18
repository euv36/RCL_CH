# Gate Tracker UI Fixes Implementation Plan

> **For agentic workers:** Execute tasks with `superpowers:subagent-driven-development`. Register exact file ownership before edits. Do not run git commands. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the reported laptop UI/tracking bugs and add an HSV mask view, Russian INFO help, and a cleaner stable interface without parallel agents colliding.

**Architecture:** Five disjoint Wave-1 workstreams (camera apply, radar window/visibility, false-gate filtering, HSV view module, help overlay) touch separate files. A single Wave-2 UI integrator then wires the new modules into `unikostyl.py` / `cv_kostyl.py`, adds Ctrl+V, and polishes the layout. No git worktrees: the working tree is dirty and most app modules are untracked. Isolation is by file ownership plus a single camera hardware lock.

**Tech Stack:** Python 3.14.10, pygame-ce 2.5.8, OpenCV 5.1.0, NumPy, pyzmq 27.3.0, Windows/MSMF UVC camera, optional Raspberry Pi stream.

**Spec:** User bug report of 2026-11-18 (preserved in session), `AGENTS.md`, `UniKostyl/AGENTS.md`, `PLAN.md`, `COORDINATION.md`.

## Global Constraints

- Work only in `C:\Users\sweetener\Desktop\camera\UniKostyl` unless a task names another path.
- Never commit, reset, checkout, stash, or clean. Preserve all existing user changes.
- Each agent edits ONLY its `Files WRITE` list. Register all shared reads. Do not edit `COORDINATION.md`; the orchestrator owns it.
- One camera hardware lock. Only T1 may open the laptop camera. All other agents use synthetic images / camera-free tests.
- Do not launch long-running GUI apps. Use bounded scripts (`timeout <= 60 s`) and close windows.
- No new third-party dependencies. Use existing pygame, cv2, numpy, zmq.
- Keep the working frame contract: RGB `numpy.uint8`, laptop portrait `720x1280`; Pi frames `640x360` when applicable.
- Do not break existing widget names or module APIs used by `cv_kostyl.py`; prefer additive APIs.
- Russian user-facing help text in UTF-8. Code comments/docstrings may be English.
- Performance guardrails from `AGENTS.md`: no per-pixel Python loops, no `cv2.findNonZero` / `boundingRect` requirement, do not add camera-resolution modes, do not re-enable auto-exposure.
- Every task ends with `python -m py_compile` on touched files and its task-specific camera-free check.

## Review Focus

- Clicking the radar window X must close it exactly once and leave no destroyed-window errors on the next frame.
- With zero configured gates, radar must draw zero gate arcs/markers even if stale Pi status or a restored `saved_gates.json` still contains gates.
- A red folder selected as a gate must stay detected while a thin red lamp post / red tape in the same LAB mask is rejected; detection must not disappear for a normal partially visible gate.
- Camera exposure/brightness changes must visibly change the next captured frame, and saved settings must apply before the first displayed frame; an unreachable Pi backend must not be reported as applied.
- HSV view must preserve the camera aspect ratio (letterbox, never stretch) and the mask overlay must align pixel-for-pixel with the camera image.
- Ctrl+V must paste clipboard text without leaking control characters; INFO must remain readable and scrollable at 1280x720 and after resize.

---

### Task 1: Live camera settings

**Files:**
- Modify: `UniKostyl/cam_settings.py`
- Modify: `UniKostyl/cv_kostyl.py`
- Modify: `UniKostyl/rpi_elp_cam.py` (only camera-settings subscription/apply region)
- Modify: `UniKostyl/rpi_cam_ctl.py`
- Modify: `UniKostyl/out/camset_check.py`
- Create: `UniKostyl/out/camset_live_probe.py`

**Interfaces:**
- Consumes: `LocalCameraCtl.apply(controls)`, `CameraSettingsWindow.pump()`, `install(...)`, `camera_settings.json`.
- Produces: same public API; settings applied on both local and Pi capture paths, with truthful apply state.

- [ ] Reproduce local exposure/brightness failure; verify auto-exposure is disabled and `cap.set` values change captured pixels.
- [ ] Apply saved settings before capture/display starts and re-apply after source switches.
- [ ] Make Pi path effective without relying on a separately started `rpi_cam_ctl.py` (subscribe/apply inside the running Pi capture service), or document and expose an unmistakable "Pi service not reachable" state.
- [ ] Add camera-free tests for control conversion/clamping plus an optional live probe behind `--live`.
- [ ] Run `out/camset_check.py --json`, py_compile; return before/after pixel evidence and exact commands.

### Task 2: Radar close and unconfigured-gate visibility

**Files:**
- Modify: `UniKostyl/radar_panel.py`
- Modify: `UniKostyl/out/radar_check.py`

**Interfaces:**
- Consumes: `set_local_results(track_results)`, `sync_from_status_panel(status_panel)`, `pump_window()`.
- Produces: X button closes the cv2 window; `_gate_data()` suppresses gates not configured/enabled in the local selection state; stale Pi gates cannot resurrect hidden gates.

- [ ] Detect cv2 window visibility every pump; on X, mark closed and destroy safely once; keep ESC and button toggle working.
- [ ] Use local gate `enabled` state as authority for visibility; zero configured gates means zero drawn gate objects.
- [ ] Add pixel/state tests for close-on-X, no repeated destroy, no-gates case with stale Pi status, configured/found normal case.
- [ ] Run `out/radar_check.py`, py_compile; return test counts and any API changes.

### Task 3: Reject false gates from one color mask

**Files:**
- Modify: `UniKostyl/gate_cluster.py`
- Modify: `UniKostyl/unikostyl.py` (only threshold-save tightening / filter call if needed)
- Modify: `UniKostyl/out/cluster_check.py`
- Create: `UniKostyl/out/false_gate_check.py`

**Interfaces:**
- Consumes: `detect_gate(mask, params, prev_x, prev_y, ...)`, `build_gate_object(...)`, `save_gate(gate_id)`.
- Produces: shape-aware component filtering shared by laptop and Pi Python pipeline; no signature break.

- [ ] Reproduce on synthetic red folder + tall lamp post + thin tape: only folder survives and becomes one gate.
- [ ] Add frame-size-relative shape filters (minimum thickness, max aspect ratio, optional edge/area guard) tuned so partially visible real goals still pass. Keep constants documented and configurable through `resolve_params` without breaking callers.
- [ ] Tighten SAVE sampling only if evidence shows the selected threshold captures unrelated red objects; prefer saturated-core sampling, not arbitrary +/- tolerance.
- [ ] Add tests covering folder alone, folder + lamp, folder + tape, folder + both, junction of pieces merging into one goal, tiny noise, partially visible goal.
- [ ] Run `out/cluster_check.py`, `out/false_gate_check.py`, py_compile; return before/after component metrics.

### Task 4: HSV mask view module

**Files:**
- Create: `UniKostyl/hsv_view.py`
- Create: `UniKostyl/out/hsv_view_check.py`

**Interfaces:**
- Produces:
  - `HSV_WINDOW_NAME = "HSV MASK"`
  - `class HsvMaskWindow:`
    - `__init__(self, enabled=False, window_size=(720, 1280))`
    - `set_frame(frame_rgb: np.ndarray, masks: dict[int, np.ndarray] | None) -> None`
    - `set_gate_colors(colors: dict[int, tuple[int,int,int]]) -> None`
    - `toggle() -> bool`, `close() -> None`, `pump(do_waitkey: bool = True) -> None`
    - `is_open` property
  - Pure helpers `build_hsv_view(frame_rgb, masks, gate_colors) -> np.ndarray` and `letterbox_to_aspect(image_bgr, target_w, target_h) -> np.ndarray`.
- Consumes: per-gate boolean masks already aligned with `frame_rgb`.
- Produces: BGR uint8 composite showing HSV representation plus gate-colored translucent mask overlay, aspect preserved, legend, and close-on-X.

- [ ] Implement pure view builder: RGB->HSV->BGR base, colored mask tint/outline, no plane warp or radar transform.
- [ ] Implement aspect-preserving letterbox; never stretch. Handle 720x1280 and 640x360.
- [ ] Implement window lifecycle with X close detection, ESC, toggle, no repeated destroy.
- [ ] Tests verify shape/dtype, alignment by checking a known mask pixel, aspect ratio math, letterbox borders, empty-mask frame, X-close state.
- [ ] Run `out/hsv_view_check.py`, py_compile; return API and screenshots/paths if generated.

### Task 5: Russian INFO overlay and content

**Files:**
- Create: `UniKostyl/help_overlay.py`
- Create: `UniKostyl/help_content.py`
- Create: `UniKostyl/out/help_check.py`

**Interfaces:**
- Produces:
  - `help_content.SECTIONS: list[dict]`, each `{"id": str, "title": str, "lines": list[str]}`; IDs: `main`, `gates`, `radar`, `camera`, `hsv`, `field`, `status`.
  - `class HelpOverlay:`
    - `__init__(self, screen_size=(1280,720))`
    - `visible: bool`
    - `toggle(section_id: str | None = None) -> None`
    - `handle_event(event) -> bool` (True = consumed)
    - `draw(surface) -> None`
    - `set_screen_size(size) -> None`
- Consumes: pygame only; no live app state.
- Produces: Russian descriptions of every button, slider, output panel, and interaction; scrollable, section tabs, readable at 1280x720 and after resize.

- [ ] Write complete Russian content for main, gates/thresholds, radar, camera settings, HSV, field/bind, status/source.
- [ ] Implement modal overlay: dark backdrop, section list/tabs, wrapped text, mouse wheel/keys scroll, close via X/ESC/INFO.
- [ ] Tests verify every section non-empty, required control labels present, wrapping width, scrolling clamps, event consumption, resize.
- [ ] Run `out/help_check.py`, py_compile; return section IDs and screenshot path.

### Task 6: UI integration, Ctrl+V, INFO/HSV buttons, polish

**Files:**
- Modify: `UniKostyl/unikostyl.py`
- Modify: `UniKostyl/widgets.py`
- Modify: `UniKostyl/cv_kostyl.py`
- Create: `UniKostyl/out/ui_integration_check.py`

**Interfaces:**
- Consumes: `hsv_view.HsvMaskWindow`, `help_overlay.HelpOverlay`, `help_content.SECTIONS`, radar/camera modules from Wave 1.
- Produces: Ctrl+V paste, INFO button, HSV button, wired mask feed, consistent layout; preserves existing widget names.

- [ ] Add Ctrl+V clipboard paste in CONNECT field; strip NUL/control chars, keep text, no crash when clipboard empty/non-text.
- [ ] Add INFO button and overlay wiring; section selection; resize handling; ESC close ordering.
- [ ] Add HSV button and feed current frame + per-gate masks only while visible; closing via X updates button state.
- [ ] Polish layout: consistent palette, spacing, hover/pressed feedback, no overlap at 1280x720/resized; do not alter tracking behavior.
- [ ] Run regressions: `out/status_check.py`, `gate_store_check.py`, `role_check.py`, `radar_check.py`, `cluster_check.py`, `selfmask_check.py`, `field_check.py`, `boundary_check.py`, `camset_check.py`, `help_check.py`, `hsv_view_check.py`, py_compile.
- [ ] Run a bounded live UI smoke; return screenshot and list of changed controls.

### Task 7: Final verification

**Files:** read-only unless a regression fix is assigned explicitly.

- [ ] Confirm no out-of-ownership edits (`git status` is read-only inspection; do not commit).
- [ ] Run full camera-free suite and report exact counts.
- [ ] Live-check camera settings, Ctrl+V, radar X, zero-gate radar, HSV alignment, INFO sections at 1280x720.
- [ ] Report remaining hardware/Pi limitations honestly.
