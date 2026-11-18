# AGENTS.md

UniKostyl: laptop-side Pygame UI for LAB color calibration + Raspberry Pi camera tracker + Arduino UART receiver. The git repo is at the repo root; no tests, CI, or linter, no package manifest beyond `requirements.txt`; verify changes by running the scripts. Root `AGENTS.md` is the overview/protocol summary; this file is the detailed hardware-verified reference.

## Entry points

- **Laptop (active client):** `python cv_kostyl.py`. Needs a display. Imports `unikostyl.py`, opens a local camera or connects to the Pi's video stream, and binds ZMQ PUB on `5557`.
- **Pi (current):** `python3 rpi_elp_cam.py` — OpenCV/V4L2 capture for the USB ELP camera. See `README_CAMERA.md` for measurements, flags, and tradeoffs.
- **Pi (legacy):** `rpi_ks.py` uses `Picamera2`, which does **not** see this USB camera (`rpicam-hello --list-cameras` prints `No cameras available!`). Do not use it with the ELP camera.
- **Legacy/experiments:** `OpenKostyl.py` (OpenMV RPC), `fisheye.py` / `fisheye_test.py`, `mirror_unwrap.py`.
- `unikostyl.main_loop_frame()` is the shared frame/UI entrypoint; it returns `(angle, {gate_id: rgb})` and must be called once per frame. Importing `unikostyl.py` creates the Pygame window and runs UI setup.

## Run / deploy

- `requirements.txt` is incomplete: `opencv-python` and `pyzmq` are used but not listed. `lib/noise_filter/noise.dll` is optional Windows-only; code falls back when it cannot load.
- Pi: Debian 13, Python 3, OpenCV 4.10, USB camera on `/dev/video0`. No X display (`DISPLAY` is unset) — do not run Pygame there.
- From this Windows box, OpenSSH key auth is not configured (`Permission denied (publickey,password)`). Use PuTTY `plink` / `pscp` with the user-supplied password. Deploy the helper modules together with the script: `pscp -batch -pw <pw> rpi_elp_cam.py gate_store.py gate_cluster.py self_mask.py distance.py twox@<pi>:/home/twox/` — `rpi_elp_cam.py` imports `gate_cluster` (Idea 5 clustering/hysteresis, core), `self_mask` (Idea 8 robot-body exclusion, optional), `gate_store` (Idea 3, optional) and `distance` (Idea 6 gate distance, optional) from its own directory.
- **The addresses move with the Wi-Fi network and are NOT stable.** On 2026-11-05 the subnet changed from `172.25.110.x` to `10.192.226.x` while the last octets stayed: laptop `.77`, Pi `.51`. Find the Pi with `arp -a | findstr <your-subnet-prefix>` (it is `d8:3a:dd…`, Raspberry Pi MAC) instead of trusting docs. Check your own IP with `ipconfig`; the Sub socket uses `--laptop-ip`, and a stale value silently means "no colors".
- Deploy + smoke test (exact form that works here, with `<pi>` = current address):
  `pscp -batch -pw <pw> rpi_elp_cam.py twox@<pi>:/home/twox/rpi_elp_cam.py`
  `plink -ssh -batch -pw <pw> twox@<pi> "cd /home/twox && python3 rpi_elp_cam.py --duration 12 --no-serial --gates 3 --laptop-ip <laptop>"`
  Confirm the copy with `sha256sum /home/twox/rpi_elp_cam.py` against the local `Get-FileHash`.
- Colors must be fed by a **PUB socket bound on the laptop** (`out/send_colors.py <sec> <port>`), because the Pi does a SUB *connect*. Port 5557 is usually already held by a running `cv_kostyl.py` — use `--rgb-port 5567` plus a sender on 5567 to test without touching it.
- `rpi_ks.py` still hardcodes a stale `LAPTOP_IP = "172.17.1.25"`; `rpi_elp_cam.py` defaults to `DEFAULT_LAPTOP_IP` and warns after 8 s of silence.
- **All gates on the Pi start disabled** (`enabled: False`): the Pi tracks nothing until the laptop sends colors via `{"gates":[...]}` (SAVE G1..G3 in the app). The "Ворота не активированы…" line every 5 s means "press SAVE in the app". Gate colors persist only until the Pi restarts.

## Camera facts (verified on this Pi + ELP camera)

- Device: `32e4:9230 "HD USB Camera"` on a USB2 hub. `/dev/video0` is capture; `/dev/video1` is metadata.
- Formats: MJPG 1920x1080@30, 1280x720@60, 1024x768@30, 800x600@60, 640x480@120; YUYV 1080p is only 6 fps. Always force MJPG. Verified homography: the 640x360 output of `rpi_elp_cam.py` covers the full 1920x1080 frame (scale 3.0, bbox 100.1% × 100.4%) — the software downscale does not crop.
- Lower modes are **not** downscales of the full frame (measured fractions of the 1080p frame, width × height):
  - 1280x720 = center crop, 66.7% × 66.6%.
  - 1280x1024 = 70.4% × 100.3% (crop width, full height); 1024x768 = 74.7% × 99.3%.
  - 640x480 = 68.1% × 90.6%; 800x600 is the narrowest at 49.8% × 66.3%.
  - Only 1920x1080 preserves the full 180° FOV. To keep FOV with a smaller output, capture 1080p and resize in software.
- Practical 1080p MJPG throughput is ~26 fps, not 30. The whole camera sits on a `480M` USB2 hub (`lsusb -t`), and at the working exposure every mode ≤1080p also tops out at ~26 fps, so cropping buys no extra frames. Set manual controls:
  `v4l2-ctl -d /dev/video0 -c auto_exposure=1 -c exposure_time_absolute=300 -c gain=0 -c brightness=0 -c exposure_dynamic_framerate=0`
  Auto exposure gave ~15 fps in low light; exposure below ~260 also drops to ~15 fps (290 → 25.9, 270 → 19.3, 260 → 16.0, 250 → 15.4). Keep exposure near 300.
- **`cv2.setNumThreads(N>1)` makes MJPG decode ~3x slower on this Pi** (fresh 1080p frame: `REDUCED_2` 15.3 ms at 1 thread vs 39.5 ms at 4; `frame_from_raw` 22.9 vs 45.8 ms). Default is now `--threads 1`. This alone was the difference between 11 and 26 fps — check it before blaming the algorithm.
- **CPU must be at 1.8 GHz.** `ondemand` drops to 600 MHz and everything is exactly 3x slower. Before benchmarking: `echo performance | sudo tee /sys/devices/system/cpu/cpufreq/policy0/scaling_governor` (needs sudo, no `cpufreq-set` installed). `vcgencmd get_throttled` showed `0x50000` and dmesg has repeated `hwmon: Undervoltage detected!` — the PSU/cable is marginal, so expect voltage-induced throttling.
- Do **not** default `gain=100`/`brightness=64` (both maxima): `brightness=64` washes colors out — HSV S drops from 168 to 20 and the image goes grey. Defaults are `gain=0, brightness=0, gamma=100, saturation=60, contrast=32`. In a dark scene raise `--gain`/`--gamma`, not `--brightness`. Control ranges: `exposure 1..5000`, `gain 0..100`, `brightness -64..64`, `gamma 72..500`, `saturation 0..128`, `contrast 0..64`, `power_line_frequency` menu 0..2 (default 1 = 50 Hz).
- There is **no automatic exposure** anymore: the software AE class was removed and the camera runs fixed manual controls. Do not reintroduce AE — it changes the picture between frames and the user needs stable real colors.
- `v4l2_set` first tries one batched `v4l2-ctl` call, then retries control-by-control on failure — a single rejected control used to silently drop gamma/saturation/contrast. A known trap: `power_line_frequency=100` is out of range, which is what a positional-argument mix-up looked like.
- `cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)` makes `VideoCapture.read()` return raw MJPEG (shape `(1,N)` uint8). Decode with `cv2.imdecode(buf, cv2.IMREAD_REDUCED_COLOR_2)` or `_4`; full 1080p decode is ~23 ms, reduced_2 ~15 ms, reduced_4 ~12 ms on this Pi.
- The camera is exclusive: `fuser -v /dev/video0` first. The user often leaves `rpi_elp_cam.py` running — check the PID and ask before killing it; every `--probe`/snapshot/benchmark fails with `can't be used to capture by name` while it holds the device. Control changes do work while OpenCV is streaming.
- Source of truth for modes: `v4l2-ctl -d /dev/video0 --list-formats-ext`; control ranges: `v4l2-ctl -d /dev/video0 -l`.

## Tracking cost (measured, 640x360, full FOV)

- Only three gate colors are ever used: yellow, blue, orange. Benchmarks with 4/6 gates are synthetic — design for 3.
- End-to-end with colors over ZMQ: 2 gates 25.9 fps, **3 gates 25.9 fps**, 6 gates 21.5 fps, 6 gates at 480x270 25.9 fps, 2 gates at 960x540 18.1 fps. 3 gates already hit the camera ceiling, so overclocking the Pi gains nothing.
- Pi 4B rev 1.5 runs 1.8 GHz, ~1 core of 4 used by the pipeline, 32 °C under load. Do not overclock for FPS: the camera caps at 26 fps.
- Idea 8 self-mask (robot body cut out of the color search): per frame it is ONE `cv2.bitwise_and` per gate (~0.03 ms at 640x360 on PC, `out/selfmask_check.py`); the keep mask is built once at startup from `self_mask.py` (bit-exact with `pi_core/src/self_mask.cpp`). The zone is configured by `--self-cx/--self-cy/--self-r/--self-r2` (px, 0 = auto: center, r = 0.22*min(w,h)) and the `"self_mask"` section of `camera_settings.json` (fractions, overrides CLI, applied after it like Idea 1 controls). Never zero HSV/BGR instead — ranges containing 0 (v_lo=0/s_lo=0/H wrap) still match black pixels and the carbon tubes would leak through.
- Benchmarks that need the camera: `out/gate_bench.py`, `out/gate_bench3.py` (3 real colors), `out/fov_check_new.py`, `out/fps_vs_crop.py`, `out/exp_quality*.py`.
- Camera-free checks (set `PYTHONPATH` to the repo root, then `python out\mask_check.py` / `python out\mask_demo.py`): verify per-pixel masking and produce `out/mask_demo.jpg`. Prefer these when the Pi is offline. Idea 5 adds `python out\cluster_check.py` (merge/stray/hysteresis/serial semantics on synthetic frames) and `python out\parity_check5.py` (Python `gate_cluster` vs C++ `pi_core` bit-for-bit on identical CC-component vectors; needs `out/pi_core_build_win/pi_core_tests.exe`, rebuildable with `python -m ziglang c++ -O2`). Idea 8 adds `python out\selfmask_check.py` (robot-body exclusion: center blob never detected / outside detected, annulus ring, HSV-zero trap regression, laptop path, status JSON, camera_settings.json round-trip, C++ keep-mask parity, cost). Idea 6 adds `python out\dist_check.py` (distance model fit/guards/occlusion-hold, status JSON additive fields, laptop RING exemption, calibrator round-trip, Python↔C++ `--dist-parity` bit-for-bit) and the calibrator `python out\dist_calibrate.py` (`--add СМ,ПХ` manual points / `--image кадр.png --dist СМ` / `--live` with camera; writes `distance_calib.json` atomically). `python out\select_check.py` — the mouse-select → threshold flow (`unikostyl.selection_rect` + `threshold_from_area` axes): regression for the 2026-11-18 inverted-mask bug (both axes were swapped, so the threshold was sampled from a transposed region and the mask highlighted everything BUT the gate).

## Data protocol

- Pi -> laptop: JPEG bytes on ZMQ PUB `tcp://*:5555`; with `--no-status` off the video message is multipart `[JPEG, JSON status]` (Idea 2). The status JSON may carry additive top-level keys: `"self_mask": {"cx","cy","r","r2"}` (Idea 8, px of the working frame, only when the robot-body mask is enabled) — the laptop parser keeps unknown top-level keys in `extra`, the `gates`/`fps` schema is unchanged. Per-gate additive fields: `"radius"` (Idea 5 cluster radius, px), `"held"` (Idea 5 hysteresis grace flag), `"dist_cm"` + `"dist_reliable"` (Idea 6 distance in cm; `dist_reliable: false` = held after occlusion/suspicious — panel and radar render it only when true; the fields disappear once the 2 s hold expires or the gate is lost).
- Pi -> Arduino: `/dev/serial0` at 115200: `G<id>,<angle>,<area>\n` and `HB\n` (every 0.5 s, `HEARTBEAT_INTERVAL` in `rpi_elp_cam.py`). **`uart_rpi.ino` currently parses only G1/G2** (plus legacy `angle,area` = gate 1); a `G3,…` line is ignored on the Arduino side even though the Pi can track 3 gates.
- Color path: laptop converts the LAB threshold midpoint to RGB; the Pi converts RGB to HSV and builds `inRange` ranges (OpenCV hue 0..179). Keep channel order straight — the Pi receives RGB, while OpenCV HSV conversion expects BGR unless converted.
- Angle: `atan2(center_y - cy, cx - center_x)` in degrees `% 360`; area thresholds are resolution-dependent.
- Per-gate hot spot: `cv2.findNonZero` + `boundingRect` allocated a coordinate array per nonzero pixel and cost several ms/gate (6 gates: 12.8 fps). They are gone — do not reintroduce them. Bbox was later removed too, since rendering is now a per-pixel mask: 6 gates reach 21.5 fps, 3 gates 25.9 fps at 640x360.
- Multi-color tracking in `unikostyl.py`: slots 1..3 (`TRACK_SLOTS`) are tracked simultaneously, each with its own angle (0° = right, CCW). A slot whose thresholds equal the stock full-range defaults is treated as not configured (`slot_configured()`): it is not drawn, not tracked, and logged as `не настроен` — otherwise the uncalibrated slot's mask covers the whole frame and overpaints the others. The processed view shows a combined per-pixel mask, colored yellow/blue/orange (`TRACK_COLORS`), with a fixed circular zone (`ZONE_RADIUS_FRAC` = 0.25 of min(w,h)) around the frame center — the zone toggles angle/distance output for the button-selected target (`TARGET G1..G3`). Distance (Idea 6): every found slot's rec carries `dist_cm`/`dist_reliable` from the shared `distance.py` module (calibrated model `d = a/(b−r)−c` from `UniKostyl/distance_calib.json`, else analytic fallback `Z = GATE_WIDTH_CM * (w/π) / w_px` with the rules goal width; occlusion hold 2 s) — the legacy ball-width estimate with `KNOWN_TARGET_WIDTH_CM` is superseded (the constants stay but are no longer used); not computed in `FISHEYE: RING` mode (strip sizes are not physical). Console prints once per `TRACK_PRINT_INTERVAL` (1 s); nothing is drawn as text on the frame.
- Rendering is a per-pixel mask, not a bounding box: `GateTracker.draw` does `np.greater(mask, 0, out=mask_bool)` then `frame[mask_bool] = color`. Center comes from `cv2.moments`; no bbox is computed. Verified offline with `out/mask_check.py` (only thresholded pixels change). The laptop mask view no longer draws `cv2.drawContours` either.
- Nothing is drawn on the video frame: no bbox, no text. The angle is printed to the console log once per second (`Углы: G1=…°, ...`, see the `angle_log_t0` block in `run()`) and sent to Arduino over Serial. The old `cv2.putText` angle label in `GateTracker.draw` was removed.

## Verify changes

- Syntax on Windows: `python -m py_compile *.py`.
- Camera mode + raw capture + decode timings: `python3 rpi_elp_cam.py --probe` (expect 1920x1080 MJPG, ~26 fps raw, reduced_2 ~15 ms at `--threads 1`).
- FOV must stay uncropped: `python3 rpi_elp_cam.py --snapshot /tmp/fov.jpg`, copy both files back, then run `out/fov_check_new.py` (expect scale ≈3.0 and coverage ≈100% × 100%). Do not trust "looks fine" — the camera's own low-res modes are crops.
- Full pipeline with detection: `python3 rpi_elp_cam.py --duration 12 --no-serial --gates 3 --laptop-ip <laptop>` (expect processing ≈25.9, capture ≈26, video ≈25.4; the log also prints per-stage ms: decode ≈23.5, hsv ≈1.1, gates ≈11.3 for 3 gates). Feed colors with `out/send_colors.py` or `out/gate_bench3.py`.
- `rpi_elp_cam.py` flags beyond the obvious: `--gates N`, `--gamma`, `--saturation`, `--contrast`, `--power-line`, `--threads`, `--track-size`, `--stream-size`, `--stream-fps`, `--no-stream`, `--rotate`.
- Stage timings live in the 5-second `FPS:` log line — read them first when FPS drops. Since Idea 5 the gates stage prints its clustering sub-stage too (`ворота X (кластер Y)`: connectedComponentsWithStats + merge/hysteresis; measured ~0.1-0.4 ms/gate net on PC — CC replaced the moments pass at the same price). If decode is ~45 ms instead of ~23, either threads > 1 or the CPU is at 600 MHz.

## Repo notes

- `widgets.py` contains Pygame UI primitives used by `unikostyl.py`.
- `thresholds.txt` stores the laptop's five LAB threshold slots; the first three are gates 1, 2, 3 (the app tracks slots 1..3 simultaneously).
- `out/` is local scratch/benchmark output, not production.
- Quick syntax check: `python -m py_compile *.py`.

## PROMPT_ROBOT phases 1–2 (UART v1 approved + implemented)

- `UniKostyl/robot_core/` — C++17 tactics port from RCJ_NEW (`rcj_arduino_src/`) with full simulator (`sim.hpp`) and unit tests (`test_logic.cpp`: 216 checks, 0 failures on Windows via zig c++). Pure logic layer (angles, omni, drive_limits, heading_ctrl, ball_logic, line_logic, gate_status, binary link codec, tactics FSM, config) is complete and tested. IO layer (termios, i2c-dev, zmq) and `main.cpp` binary are `// UNVERIFIED ON HW:` — compiled only on Linux/Pi.
- `UniKostyl/mega_fw/` — thin Arduino Mega 2560 executor firmware (PlatformIO). Implements: motors safety path, gyro DMP + drift-freeze, RAW 16-line ADC polling (no thresholds/angle on the Mega), catcher/dribbler/kicker executors, 300 ms watchdog. Binary UART v1: `T_DRIVE`/`T_STOP` RX, `T_HDG`/`T_LINE`/`T_HB` TX, CRC-8/ATM, resync scanner (`link_frame.hpp` + `link_rx.cpp`/`link_tx.cpp`).
- `UniKostyl/mega_fw/PROTOCOL.md` — **APPROVED v1 (2026-11-18)**, fixed binary + CRC8. Frame catalog: T_DRIVE (0x01), T_STOP (0x02), T_HDG (0x11), T_LINE (0x12), T_HB (0x13). Implemented in `robot_core/src/link_codec.*` (Pi) and `mega_fw/src/link_frame.hpp`/`link_{rx,tx}.cpp` (Mega); PC conformance tests green; not yet run on hardware.
- Prebuilt artifacts: `UniKostyl/mega_fw/.pio/build/megaatmega2560/firmware.elf` (45912 B) and `firmware.hex` (64961 B) from the 2026-11-18 PlatformIO build.
- Locator voltage warning: Mega is 5 V logic, Pi I2C is 3.3 V — level shifter required, NOT yet measured (hardware step for user).
