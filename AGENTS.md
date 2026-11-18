# AGENTS.md

Camera color-calibration/tracking system split across laptop UI, Raspberry Pi, and Arduino. All source lives in `UniKostyl/`; no test CI — verification is the camera-free check suites in `UniKostyl/out/` plus live runs. Verified on hardware where noted.

## Docs map

- `UniKostyl/AGENTS.md` — detailed, hardware-verified reference: camera modes/FOV, FPS benchmarks, Pi tuning, deploy commands, per-idea notes. **Prefer it over this file when facts conflict.**
- `UniKostyl/README_CAMERA.md` — ELP camera measurements and `rpi_elp_cam.py` flags (incl. Idea 5/6/8 flag tables).
- `PLAN.md` — one-page project plan + live pipeline status («Статус конвейера»).
- `PROMPT_EN.md` / `PROMPT.md` — master prompts (EN/RU) for parallel implementation agents: global rules, contracts, the 12 ideas. `PROMPT_ROBOT.md` — separate prompt for the tactics-port workstream.
- `COORDINATION.md` — parallel-agent file/hardware ownership registry. Read it before editing shared files.
- `UniKostyl/rcj_arduino_src/` — READ-ONLY reference copy of the old all-in-one Arduino Mega firmware (RCJ_NEW) being split (see WIP below).
- `UniKostyl/out/` — check suites + bench scripts (in git); probe images/previews are scratch.

## Current vs legacy

- **Laptop UI (current):** `python UniKostyl/cv_kostyl.py` or double-click **`camera.bat`** (the old tuner moved to `camera_tune.bat`). Auto-detects the ELP (the camera that delivers 1920x1080; known index 1), opens 1080p MJPG (lower modes are sensor crops), software-downscales to 720x1280 after a 90° CCW rotate. Per-5-s console line `[FPS] ui X | cap/pre/ui/pump ms` — read it first when the UI feels slow.
- **Pi (current, Python):** `python3 rpi_elp_cam.py` — full pipeline with Ideas 2/3/5/6/8/9 (multipart status, saved gates, clustering+hysteresis, distance, self-mask, roles). Helper modules deployed next to it: `gate_store.py gate_cluster.py self_mask.py distance.py`.
- **Pi (C++ core, ready, NOT yet deployed — Pi offline):** `UniKostyl/pi_core/` — full C++17 replacement of `rpi_elp_cam.py` (V4L2, ZMQ, serial contract, saved_gates, clustering, self-mask, distance). Logic unit-tested on PC (540 checks; HSV bit-parity with cv2 over 200k points). Build on the Pi via its `deploy.sh` (needs `libzmq3-dev`; password from user). `rpi_elp_cam.py` stays as the rollback path.
- **PROMPT_ROBOT phases 1–2 (UART v1 approved + implemented):** `UniKostyl/mega_fw/` (thin Mega 2560 executor, compiled `firmware.hex/elf` via PlatformIO, `PROTOCOL.md` APPROVED v1 — binary frames + CRC8, implemented on both sides) and `UniKostyl/robot_core/` (C++17 tactics port from RCJ_NEW with simulator; PC tests 216/0 on Windows; mega_fw host framing tests 21/0). IO layer (termios/i2c-dev/zmq) and the main binary are `// UNVERIFIED ON HW:` — compiled only, no real hardware tested. Pi deploy + hardware bench pending.
- **Legacy:** `rpi_ks.py` (Picamera2, stale IP), `OpenKostyl.py`, `fisheye_test.py` (tuner, `camera_tune.bat`), `mirror_unwrap.py`, `thresholds.txt` (world of `OpenKostyl.py` only).
- Arduino rollback firmware: `uart_rpi.ino` (parses `G1`/`G2` + legacy `angle,area`).

## Running

- Laptop: system Python (repo-root `.venv` is WSL-only). Installed here: pygame-ce 2.5.8, pyzmq, opencv-python, numpy, pillow (Python 3.14 has no wheel for the pinned `pygame==2.6.1`; consider relaxing the pin). `lib/noise_filter/noise.dll` optional Windows-only.
- UI buttons (top row): SAVE G1..G3 (color+role to Pi + `saved_gates.json`), CAM SET (settings window: exposure etc., roles G1..G3), RADAR (separate resizable cv2 window, ESC closes), BIND FIELD + POS (field snapshot → `field_map.json`), TARGET G1..G3, pause. Status panel: gates found/lost live, angle/area/dist, source, PUB 5557, FPS; role badges НАШИ/ПРОТИВНИК.
- Pi deploy from Windows: PuTTY `pscp`/`plink` with the user-supplied password (user `twox`); exact commands in `UniKostyl/AGENTS.md`. **IPs move with the Wi-Fi** — find the Pi by MAC prefix `d8:3a:dd` via `arp -a` (2026-11-17: subnet 192.168.0.x, laptop .199, Pi offline).

## Network and serial protocol

- Pi publishes on `tcp://*:5555`: multipart `[JPEG, JSON status]` (default) or single JPEG (`--no-status`). Status: `{"gates":{"1":{"enabled","found","angle","area","radius"?,"held"?,"dist_cm"?,"dist_reliable"?,"role"?},...},"fps":N}` + additive top-level `self_mask`/`edge_sectors` — add fields, never rename/remove. Laptop accepts both forms.
- Colors: laptop PUB `tcp://*:5557`, JSON `{"gates":[{"id":N,"rgb":[..],"role"?}..]}` + legacy `{"rgb":[..]}` (gate 1); test on 5567 (`out/send_colors.py`). Camera-control channel: laptop PUB 5559 → Pi `rpi_cam_ctl.py` (auto-exposure can never be re-enabled).
- Serial Pi→Arduino `/dev/serial0` @115200: `G<id>,<angle>,<area>\n` (≥3° wrapped change, min 0.10 s), `G<id>,-1,0\n` once on loss (after the Idea-5 hysteresis: 5 empty frames), `HB\n` every 0.5 s. No Arduino→Pi channel yet (designed in `mega_fw/PROTOCOL.md`, needs approval).
- Contract files (all atomic tmp+fsync+rename): `saved_gates.json` (gate colors+LAB+role; Pi restarts with gates active, no laptop needed), `camera_settings.json` (controls + `self_mask` section), `distance_calib.json` (d = a/(b−r)−c), `field_map.json` (field binding + boundary thresholds). `distance_calib.json` is created by the user running `out/dist_calibrate.py`; floor thresholds by `out/floor_calibrate.py`.

## Verify (camera-free, from UniKostyl/)

`python out/<suite>.py` — `status_check`, `gate_store_check`, `cluster_check`, `role_check`, `selfmask_check`, `radar_check`, `dist_check`, `field_check`, `boundary_check`, `select_check`, `camset_check --json`, plus `parity_check5` (Python↔C++ clustering bit-parity, needs `out/pi_core_build_win/pi_core_tests.exe`, rebuild via `python -m ziglang c++ -O2`) and `mask_check`/`mask_demo` (PYTHONPATH=UniKostyl). Live acceptance: `python out/acceptance_check.py` (red folder + green towel scene, 15 checks; the real pipeline on the live ELP). Syntax: `python -m py_compile *.py`.

## Module quirks

- `unikostyl.py` initializes pygame + widgets at import (never import headless); `main_loop_frame(frame_rgb)` → `(angle, {gate_id: rgb})` once per frame. Nothing is ever drawn on the video frame itself — status lives in widgets/console only. Slot defaults = not configured (`slot_configured()`), not tracked.
- Channel order: Pi receives RGB; OpenCV conversions expect BGR. Hue wrap 0/179 handled everywhere; angle changes are wrap-aware (359→1 = 2°).
- `ImageNumpy.draw` (widgets.py) uses persistent surfaces + `pixelcopy.array_to_surface` — do not revert to per-frame `make_surface`+`scale` (that cost ~19 ms/frame). Preprocess order: resize BEFORE rotate.
- Radar window is a cv2 `WINDOW_NORMAL` window pumped from the main loop (`pump_window`; waitKey coordinated with the CAM SET window).
- Forbidden patterns (hot spots): `cv2.findNonZero`/`boundingRect`, per-pixel Python, `cv::setNumThreads(N>1)` on the Pi, auto-exposure re-enable, low camera modes as "downscale" (sensor crops).
