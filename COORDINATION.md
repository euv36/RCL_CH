# COORDINATION.md

Parallel-agent coordination for PROMPT_EN.md ideas. Register before working, update status when done.
Format: agent/task | status | files WRITE | files READ. Hardware (laptop camera, Pi) is a resource too — separate lock lines.

## Agents

| Agent / task | Status | Files WRITE | Files READ |
|---|---|---|---|
| Idea 1 — camera settings window (`cam_settings.py`, `rpi_cam_ctl.py`, persistence, CAM SET button) | done (PC + live local camera verified; Pi deploy of `rpi_cam_ctl.py` pending — NEEDS: Pi password) | `UniKostyl/cam_settings.py` (new), `UniKostyl/rpi_cam_ctl.py` (new), `UniKostyl/cv_kostyl.py` (pinpoint: import + install + pump + close), `UniKostyl/rpi_elp_cam.py` (pinpoint: `apply_camera_settings_file` helper + call in `run()` after CLI), `UniKostyl/out/camset_check.py` (new), `UniKostyl/out/camset_ui_smoke.py` (new), `camera_settings.json` (runtime contract file, UniKostyl/ and /home/twox/) | `UniKostyl/unikostyl.py`, `UniKostyl/fisheye.py`, `UniKostyl/widgets.py`, `UniKostyl/rpi_elp_cam.py`, `UniKostyl/AGENTS.md`, root AGENTS.md, PROMPT_EN.md |
| Idea 4 — C++ core `UniKostyl/pi_core/` (full replacement of `rpi_elp_cam.py`: V4L2 MJPG 1080p capture, REDUCED_COLOR_2 decode, HSV+moments 3 gates, ZMQ 5555/5557, serial contract, `saved_gates.json` atomic load/save, CLI + `--print-serial`) | done (code + local tests; Pi build/smoke pending — NEEDS: Pi password) | `UniKostyl/pi_core/**` (new dir: `src/`, `tests/`, `vendor/`, `CMakeLists.txt`, `deploy.sh`) | `UniKostyl/rpi_elp_cam.py`, `UniKostyl/cv_kostyl.py`, `UniKostyl/unikostyl.py`, `UniKostyl/AGENTS.md`, `UniKostyl/README_CAMERA.md`, root `AGENTS.md`, `PROMPT_EN.md`, `PLAN.md`, `COORDINATION.md`. Does NOT touch `rpi_elp_cam.py` or Idea 1 files. |
| Idea 2 — clear UI: gate status panel (configured/found live/angle/area/FPS, source, PUB 5557) + Pi multipart `[JPEG, JSON status]` on 5555 | done | `UniKostyl/status_panel.py` (NEW), `UniKostyl/cv_kostyl.py` (pinpoint: multipart recv + status parse + 5557 publish state), `UniKostyl/unikostyl.py` (pinpoint: panel widget + `set_pi_status`/`set_video_source`/`set_publish_state`/`note_colors_sent`), `UniKostyl/rpi_elp_cam.py` (pinpoint: Streamer multipart + `build_gate_status` + `--no-status`), `UniKostyl/out/status_check.py` (scratch test) | PROMPT_EN.md, PROMPT.md, PLAN.md, root AGENTS.md, `UniKostyl/AGENTS.md`, `UniKostyl/widgets.py`, `UniKostyl/unikostyl.py`, `UniKostyl/cv_kostyl.py`, `UniKostyl/rpi_elp_cam.py`, `UniKostyl/out/mask_check.py` |
| Idea 3 — persist 3 gate colors (`saved_gates.json` atomic, both sides; SAVE Gn overwrites slot n; laptop startup restores slots+LAB and auto-sends to Pi; Pi startup loads its copy, updates it on colors from 5557) | done (PC verified; Pi restart smoke pending — NEEDS: Pi password) | `UniKostyl/gate_store.py` (NEW: load/upsert/atomic save, no deps beyond stdlib), `UniKostyl/cv_kostyl.py` (pinpoint: import + startup restore + periodic auto-send + persist in SAVE branch), `UniKostyl/rpi_elp_cam.py` (pinpoint: import + startup load after tracker creation + persist in RGBListener + flush_if_due in main loop), `UniKostyl/out/gate_store_check.py` (scratch test), `UniKostyl/saved_gates.json` + `/home/twox/saved_gates.json` (runtime contract files) | PROMPT_EN.md, PLAN.md, root AGENTS.md, `UniKostyl/AGENTS.md`, `UniKostyl/cv_kostyl.py`, `UniKostyl/unikostyl.py`, `UniKostyl/rpi_elp_cam.py`, `UniKostyl/widgets.py`, `UniKostyl/out/status_check.py`, COORDINATION.md |
| Idea 9 — gate roles ours/opponent/unset (CAM SET role selector, role in saved_gates.json + color message 5557 + status JSON, НАШИ/ПРОТИВНИК badges in status panel) | done (PC verified: out/role_check.py 62/62, status_check + gate_store_check regressions green, pi_core tests 466/0; Pi wire delivery UNVERIFIED ON HW — needs deploy of rpi_elp_cam.py + gate_store.py) | `UniKostyl/gate_store.py` (additive: normalize_role/stored_role/roles_map/upsert role param/set_role/set_rgb role param/GateStore.roles+get_role), `UniKostyl/cam_settings.py` (additive: GATE ROLES section in CAM SET window + install kwargs gate_store_obj/roles_changed_cb/gate_colors), `UniKostyl/cv_kostyl.py` (pinpoint: build_gate_payload roles param, roles in auto-send + SAVE payloads, push_roles_to_panel, install args), `UniKostyl/status_panel.py` (additive: set_roles + НАШИ/ПРОТИВНИК badges in pi+local rows), `UniKostyl/rpi_elp_cam.py` (pinpoint ONLY: RGBListener role pass-through + _effective_role fallback, startup role restore in run(), build_gate_status "role" field — tracker hot path untouched), `UniKostyl/pi_core/src/color_message.{hpp,cpp}` (role parse) + `color_listener.cpp` (role to tracker+store) + `gate_logic.hpp` (GateRole enum + GateParams.role) + `gate_store.{hpp,cpp}` (set_gate_from_network role param) + pinpoint `gate_tracker.{hpp,cpp}` (set_gate_role) + `main.cpp` (role restore line) + `tests/test_logic.cpp` (appended checks), `UniKostyl/out/role_check.py` (NEW) | PROMPT_EN.md, PLAN.md, root+UniKostyl AGENTS.md, COORDINATION.md, `UniKostyl/cam_settings.py`, `UniKostyl/gate_store.py`, `UniKostyl/status_panel.py`, `UniKostyl/cv_kostyl.py`, `UniKostyl/rpi_elp_cam.py`, `UniKostyl/unikostyl.py` (READ-only), `UniKostyl/pi_core/src/*`, `UniKostyl/out/status_check.py`, `UniKostyl/out/gate_store_check.py`, `UniKostyl/out/camset_*.py` |
| Idea 8 — robot self-mask: exclusion zone cut out of the color search BEFORE inRange consumption on all 3 pipelines (`unikostyl.py` compute_track, `rpi_elp_cam.py` GateTracker.process, `pi_core` gate_tracker.cpp), `self_mask` section in camera_settings.json, CLI `--self-cx/--self-cy/--self-r/--self-r2`, additive `self_mask` key in the Idea 2 status JSON | done (PC verified: out/selfmask_check.py all green incl. bit-exact C++ keep-mask parity, pi_core_tests 501/0, regressions cluster/status/gate_store/mask/role/camset/parity5 green, cost +0.072 ms/frame; default zone covers the Pi PCB 100% on out/scene_red_folder.png — out/selfmask_preview.png; Pi deploy + live smoke UNVERIFIED ON HW — Pi offline, NEEDS: Pi password) | `UniKostyl/self_mask.py` (NEW: resolve + keep-mask builder, shared Pi/laptop), `UniKostyl/rpi_elp_cam.py` (pinpoint: GateTracker mask zero-out + CLI flags + camera_settings.json self_mask load + build_gate_status extra key), `UniKostyl/unikostyl.py` (pinpoint: compute_track applies keep-mask before detect_gate; RING mode excluded), `UniKostyl/pi_core/src/self_mask.{hpp,cpp}` (NEW pure), `UniKostyl/pi_core/src/gate_tracker.{hpp,cpp}` (set_self_mask + zero-out), `UniKostyl/pi_core/src/config.{hpp,cpp}` (4 flags + --no-self-mask), `UniKostyl/pi_core/src/main.cpp` (resolve + log), `UniKostyl/pi_core/tests/test_logic.cpp` (append: test_self_mask + --selfmask-parity), `UniKostyl/pi_core/CMakeLists.txt` (+self_mask.cpp), `UniKostyl/out/selfmask_check.py` (NEW), `UniKostyl/out/selfmask_preview.py` (NEW), `UniKostyl/out/cluster_check.py` (pinpoint: section E disables the default self-mask while testing Idea 5 clustering alone), `UniKostyl/camera_settings.json` (self_mask section, created), `UniKostyl/README_CAMERA.md`, `UniKostyl/AGENTS.md`, COORDINATION.md | PROMPT_EN.md, PLAN.md, root+UniKostyl AGENTS.md, COORDINATION.md, `UniKostyl/gate_cluster.py` (READ-only — mask goes before its detect_gate), `UniKostyl/rpi_elp_cam.py`, `UniKostyl/unikostyl.py`, `UniKostyl/cv_kostyl.py` (READ-only), `UniKostyl/cam_settings.py` (READ-only — window trackbars are a follow-up pass, see Idea 8 notes), `UniKostyl/status_panel.py` (READ-only), `UniKostyl/pi_core/src/*`, `UniKostyl/out/status_check.py`, `UniKostyl/out/gate_store_check.py`, `UniKostyl/out/mask_check.py`, `UniKostyl/out/role_check.py`, `UniKostyl/out/scene_red_folder.png` |
| Idea 5 — gate blob clustering + loss hysteresis (morph OPEN, merge gap, min area, stray reject, at most 1 gate/color, lost-after-N-frames / confirm-after-M-frames, stick-to-previous-position; Python `rpi_elp_cam.py` + `unikostyl.py`, C++ parity in `pi_core`) | done (PC verified: py_compile, synthetic suite, Python-vs-C++ bit-for-bit parity 4 seeds, status/gate_store/mask regressions green, cost neutral ~+0.1 ms/gate; Pi deploy + live smoke pending — NEEDS: Pi password, Pi offline) | `UniKostyl/gate_cluster.py` (NEW: pure clustering+hysteresis, numpy/cv2), `UniKostyl/rpi_elp_cam.py` (pinpoint: GateTracker.process pipeline + CLI flags + stage timings; build_gate_status radius/held fields), `UniKostyl/unikostyl.py` (pinpoint: compute_track clustering path), `UniKostyl/pi_core/src/gate_cluster.{hpp,cpp}` (NEW pure), `UniKostyl/pi_core/src/gate_tracker.{hpp,cpp}` (rewritten process: CC+cluster+stability), `UniKostyl/pi_core/src/config.{hpp,cpp}` (6 new flags), `UniKostyl/pi_core/src/main.cpp` (params + log), `UniKostyl/pi_core/tests/test_logic.cpp` (append: cluster tests + --cluster-parity), `UniKostyl/pi_core/CMakeLists.txt` (+gate_cluster.cpp), `UniKostyl/out/cluster_check.py` (NEW), `UniKostyl/out/parity_check5.py` (NEW), `UniKostyl/README_CAMERA.md` (flags table), `UniKostyl/AGENTS.md` (3 pinpoint lines) | PROMPT_EN.md, PLAN.md, root+UniKostyl AGENTS.md, `UniKostyl/rpi_elp_cam.py`, `UniKostyl/unikostyl.py`, `UniKostyl/pi_core/src/*`, `UniKostyl/out/status_check.py`, `UniKostyl/out/gate_store_check.py`, COORDINATION.md |

| Idea 6 — gate distance from gate size: `distance.py` (pure model d=a/(b-r)-c + fit + guards + occlusion hold), `distance_calib.json` (atomic), calibrator `out/dist_calibrate.py` (live/manual points), wiring rpi_elp_cam.py + unikostyl.py + pi_core `distance.{hpp,cpp}` + GateResult dist fields, additive `dist_cm`/`dist_reliable` in status JSON, camera-free tests `out/dist_check.py` | done (PC verified: out/dist_check.py 0 failed — fit round-trip, guards/clamps, occlusion hold, status JSON additive, laptop path + RING exemption, calibrator round-trip, Python↔C++ `--dist-parity` bit-for-bit; pi_core_tests 540/0; regressions mask/status/gate_store/role/cluster/selfmask/camset/parity_check5/radar_check green; cost ~0.004 ms per gate eval. Pi deploy + real-field calibration UNVERIFIED ON HW — NEEDS: Pi password + user calibration on the venue) | `UniKostyl/distance.py` (NEW), `UniKostyl/out/dist_calibrate.py` (NEW), `UniKostyl/out/dist_check.py` (NEW), `UniKostyl/distance_calib.json` (runtime contract file, created by calibrator — deliberately NOT committed), `UniKostyl/rpi_elp_cam.py` (PINPOINT: dist fields in build_gate_status + calib load at startup + hold state per gate + 6 CLI flags), `UniKostyl/unikostyl.py` (PINPOINT: compute_track rec gains dist_cm/dist_reliable + GATE_WIDTH_CM/DIST_* constants + lazy `_dist_est`; Idea 7 widget regions untouched), `UniKostyl/pi_core/src/distance.{hpp,cpp}` (NEW pure), `UniKostyl/pi_core/src/gate_tracker.{hpp,cpp}` (GateResult dist fields + per-gate DistEstimator), `UniKostyl/pi_core/src/config.{hpp,cpp}` (6 flags), `UniKostyl/pi_core/src/main.cpp` (load calib + log), `UniKostyl/pi_core/tests/test_logic.cpp` (test_distance + --dist-parity), `UniKostyl/pi_core/CMakeLists.txt` (+distance.cpp), `UniKostyl/README_CAMERA.md` + `UniKostyl/AGENTS.md` (flags/docs), COORDINATION.md | PROMPT_EN.md, PLAN.md, root+UniKostyl AGENTS.md, COORDINATION.md, `UniKostyl/gate_cluster.py` (radius semantics), `UniKostyl/rpi_elp_cam.py`, `UniKostyl/unikostyl.py`, `UniKostyl/status_panel.py` (READ-only — dist passthrough verify), `UniKostyl/self_mask.py` (READ-only), `UniKostyl/pi_core/src/*`, `UniKostyl/out/status_check.py`, `out/cluster_check.py`, `out/selfmask_check.py`, `out/gate_store_check.py`, `out/role_check.py`, `out/mask_check.py`, `out/camset_check.py`, `out/parity_check5.py`, `out/radar_check.py`. Does NOT touch: radar_panel.py, out/radar_check.py, widget-registration regions of unikostyl.py, cv_kostyl.py, field_map.py. |
| Idea 7 — radar panel («кружочек»): togglable panel in the main UI — black disc + rim, light body zone (mirrors Idea 8 SELF_MASK r), gate arcs of their color at tracking angles (Pi status via status_panel — no duplicate parsing; local via track_results), target marker + "angle°" + "r = N px → M cm" footer (dist_cm from Idea 6 passthrough), green/red role badge arcs (unset == absent), stale-status fallback like status_panel, zero cost when hidden | done (PC verified: out/radar_check.py 77/77 — pixel-verified arcs at angles incl. 358° wrap, marker at selected gate, role badges present/absent per unset invariant, stale>2 s fallback to local, held dimmed arcs; regressions status_check/gate_store_check/role_check/cluster_check green; costs: draw 0.09 ms visible / 0.0002 ms hidden / 0.003 ms feed; live Pi-stream and live-camera view UNVERIFIED ON HW — needs Pi deploy + a human at the laptop display) | `UniKostyl/radar_panel.py` (NEW), `UniKostyl/unikostyl.py` (PINPOINT: guarded import, `toggle_radar()` near `set_pause`, radar+btn_radar registration next to status_panel block, rect in `update_all_widgets`, feed block 5c in `main_loop_frame`), `UniKostyl/out/radar_check.py` (NEW), COORDINATION.md | PROMPT_EN.md, PLAN.md, root+UniKostyl AGENTS.md, COORDINATION.md, `UniKostyl/widgets.py`, `UniKostyl/status_panel.py` (READ-only — pi_status/source_kind/roles synced from its widget), `UniKostyl/gate_store.py` (roles semantics), `UniKostyl/gate_cluster.py`, `UniKostyl/cv_kostyl.py` (READ-only — roles flow via status_panel.set_roles), `UniKostyl/cam_settings.py` (READ-only — btn position x=530), `UniKostyl/self_mask.py` (READ-only), `UniKostyl/out/status_check.py`, `out/gate_store_check.py`, `out/role_check.py`, `out/cluster_check.py`. Did NOT touch: rpi_elp_cam.py, pi_core, cam_settings.py, self_mask.py, cv_kostyl.py. |
| Idea 10+11 — field binding (BIND FIELD button → field_map.json snapshot of gate angle/dist + robot_pos preset + heading; radar "last known est" marks while gate out of view) + field boundary camera adviser (green floor HSV per-sector edge detector, hysteresis 2-3 frames, advisory only, field_map.json "boundary" thresholds, fusion placeholder for the Idea 12 reverse channel) | done (PC verified: out/field_check.py all green — atomic binding round-trip on the REAL contract file incl. restart restore + pristine-file restore, preset validation, corrupt→WARN, radar est marks pixel-verified, edge arcs pixel-verified, est map 0.033 ms; out/boundary_check.py all green — sector geometry on synthetic green/white/dark frames incl. portrait aspect, hysteresis confirm-2/release-3, no false positives on uniform field/shadow/white line, hue-wrap floor, fusion board-priority, 0.89 ms/frame at 720x1280; regressions status/gate_store/role/radar/selfmask/cluster green; live binding + real-field calibration UNVERIFIED ON HW — NEEDS: user at the venue) | `UniKostyl/field_map.py` (NEW: field_map.json load/save atomic + snapshot builders + estimated directions + boundary section), `UniKostyl/field_boundary.py` (NEW: camera-side sector edge detector + hysteresis + fusion placeholder + status_extra), `UniKostyl/field_map.json` (runtime contract file, created: empty binding + default boundary thresholds), `UniKostyl/unikostyl.py` (PINPOINT: imports, FIELD_MAP/BIND globals + load_field_map_settings, bind_field/cycle_robot_pos/set_bind_heading, btn_bind+btn_pos registration + rects, est marks in per-second log, radar est+edge feed, boundary step 5d), `UniKostyl/radar_panel.py` (PINPOINT: set_estimated + est merge in `_gate_data()` + dimmed arcs + "(est)" footer + set_edge_sectors + EDGE display), `UniKostyl/out/field_check.py` (NEW), `UniKostyl/out/boundary_check.py` (NEW), COORDINATION.md | PROMPT_EN.md, PLAN.md, root+UniKostyl AGENTS.md, COORDINATION.md, `UniKostyl/unikostyl.py` (re-read before each edit — Idea 6 landed dist fields mid-task, no conflicts), `UniKostyl/radar_panel.py`, `UniKostyl/status_panel.py` (READ-only — PI_STATUS_STALE_S + parse passthrough), `UniKostyl/gate_store.py` (atomic-write pattern), `UniKostyl/self_mask.py` (guard pattern), `UniKostyl/out/radar_check.py`, `out/status_check.py`, `out/gate_store_check.py`, `out/role_check.py`, `out/selfmask_check.py`, `out/cluster_check.py`, `out/scene_red_folder.png`. Did NOT touch: rpi_elp_cam.py, pi_core, cam_settings.py, cv_kostyl.py, status_panel.py, Idea 6 regions. |
| PROMPT_ROBOT phases 1–2 — RCJ logic split (Idea 12): thin Mega firmware skeleton `mega_fw/` (motors safety path, MPU6050 DMP + drift-freeze, raw lines, kicker/dribbler/catcher executors, watchdog), Pi tactics core `robot_core/` (ball/line/tactics FSM + omni + PI/PD heading, camera-status JSON consumption) + PC simulator/unit tests via zig; approved UART v1 (binary frames + CRC8) implemented on both sides | done (phase 1: robot_core 183/0 tests on Windows/zig; mega_fw firmware.hex/elf compiled via PlatformIO. phase 2, 2026-11-18: PROTOCOL v1 approved by user and implemented — `link_frame.hpp` + `link_rx.cpp`/`link_tx.cpp` (Mega), `link_codec.*` + `uart_link_linux.*` (Pi). Tests: robot_core 216/0, mega_fw host 21/0; PlatformIO compile SUCCESS. Pi deploy + hardware bench pending) | `UniKostyl/mega_fw/**` (NEW: PROTOCOL.md, platformio.ini, README.md, src/, tests/), `UniKostyl/robot_core/**` (NEW: CMakeLists.txt, README.md, src/, tests/, vendor/), COORDINATION.md, PLAN.md | PROMPT_ROBOT.md, PROMPT_EN.md (§4-5), PLAN.md, root AGENTS.md, `UniKostyl/AGENTS.md`, `UniKostyl/rcj_arduino_src/**` (READ-ONLY reference), COORDINATION.md, `UniKostyl/pi_core/` (READ-only: vendor/nlohmann copy, test/build pattern). Does NOT touch camera files (cv_kostyl.py, unikostyl.py, rpi_elp_cam.py, gate_*.py, self_mask.py, status_panel.py, radar_panel.py, fisheye*, widgets.py, out/, field_map.py), pi_core, PROMPT*.md; no git commands. |
| 2026-11-18 T1 `cam-settings-live` - make saved/live camera exposure, gain, brightness etc. actually affect output; Pi apply path without a separately started service; truthful apply state | done-inline (PC verified; Pi live apply UNVERIFIED; Pi must run updated `rpi_elp_cam.py` with 5559 listener or `rpi_cam_ctl.py`) | `UniKostyl/cam_settings.py`, `UniKostyl/cv_kostyl.py`, `UniKostyl/rpi_elp_cam.py` (camera-settings region only), `UniKostyl/rpi_cam_ctl.py`, `UniKostyl/out/camset_check.py`, `UniKostyl/out/camset_live_probe.py` (NEW) | `UniKostyl/unikostyl.py`, `UniKostyl/fisheye.py`, `UniKostyl/camera_settings.json`, `UniKostyl/AGENTS.md`, root `AGENTS.md`, COORDINATION.md. Holds laptop camera lock. Does NOT touch `unikostyl.py`, `radar_panel.py`, `gate_cluster.py`, `hsv_view.py`, `help_*.py`. |
| 2026-11-18 T2 `radar-close-visibility` — radar X closes; no configured gates ⇒ no arcs even with stale Pi status/restored gates | done-inline (radar_check green; title-bar X mocked, not live-clicked) | `UniKostyl/radar_panel.py`, `UniKostyl/out/radar_check.py` | `UniKostyl/unikostyl.py`, `UniKostyl/status_panel.py`, `UniKostyl/gate_store.py`, `UniKostyl/out/status_check.py`, `UniKostyl/out/gate_store_check.py`, COORDINATION.md. No camera; does NOT touch unikostyl.py. |
| 2026-11-18 T3 `false-gate-filter` - reject thin/lamp/tape components in same color mask; preserve one real gate; shape-aware shared Python filter | done-inline (Python false_gate/cluster green; C++ direct build/tests green, 540/0; parity 61/61 OK) | `UniKostyl/gate_cluster.py`, `UniKostyl/unikostyl.py` (pinpoint threshold/filter only), `UniKostyl/out/cluster_check.py`, `UniKostyl/out/false_gate_check.py` (NEW), `UniKostyl/pi_core/src/gate_cluster.hpp`, `UniKostyl/pi_core/src/gate_cluster.cpp` | `UniKostyl/rpi_elp_cam.py`, `UniKostyl/self_mask.py`, `UniKostyl/cam_settings.py`, `UniKostyl/out/scene_red_folder.png`, `UniKostyl/out/parity_check5.py`, docs, COORDINATION.md. No camera; does NOT touch radar/cam_settings/hsv/help files. |
| 2026-11-18 T4 `hsv-mask-view` - standalone HSV view with aligned gate-mask overlay, aspect preserved, X/ESC close | done-inline (hsv_view_check green; title-bar X mocked, not live-clicked) | `UniKostyl/hsv_view.py` (NEW), `UniKostyl/out/hsv_view_check.py` (NEW) | `UniKostyl/unikostyl.py`, `UniKostyl/gate_cluster.py`, `UniKostyl/out/scene_red_folder.png`, `AGENTS.md`, COORDINATION.md. No edits to existing app files. |
| 2026-11-18 T5 `russian-info-help` - standalone Russian INFO overlay + content for every menu/control/output | done-inline (help_check green; integrated into UI, see T6) | `UniKostyl/help_overlay.py` (NEW), `UniKostyl/help_content.py` (NEW), `UniKostyl/out/help_check.py` (NEW) | `UniKostyl/unikostyl.py`, `UniKostyl/widgets.py`, `UniKostyl/cam_settings.py`, `UniKostyl/radar_panel.py`, `UniKostyl/status_panel.py`, `UniKostyl/field_map.py`, `UniKostyl/PLAN.md`, docs, COORDINATION.md. No edits to existing app files. |
| 2026-11-18 T6 `ui-integration` - Ctrl+V, INFO/HSV buttons, feed masks, cohesive stable layout | done-inline (ui_integration_check + full camera-free suite green; live smoke captured: live_smoke_final2.png; CAM SET/RADAR overlap fixed in cam_settings.py) | `UniKostyl/unikostyl.py`, `UniKostyl/widgets.py`, `UniKostyl/cv_kostyl.py`, `UniKostyl/out/ui_integration_check.py` (NEW), `UniKostyl/status_panel.py`, `UniKostyl/radar_panel.py` | `UniKostyl/hsv_view.py`, `UniKostyl/help_overlay.py`, `UniKostyl/help_content.py`, `UniKostyl/cam_settings.py`, `UniKostyl/gate_cluster.py`, all `out/*check.py`, COORDINATION.md. No camera unless lock free. |

## Hardware locks

| Resource | Holder | Since | Note |
|---|---|---|---|
| camera (laptop ELP) | free | - | 2026-11-18: no live action taken; T1 PC fix only, Pi unverified. Released after inline work. |
| camera live UI | free | - | T6 completed offline/with dummy SDL; no live camera taken. |
| pi | free | - | Idea 1 does NOT deploy to Pi (no password); only prepares `rpi_cam_ctl.py` + deploy commands |

## needs integration

- T1: Pi output is unverified until Pi runs updated `UniKostyl/rpi_elp_cam.py` with CAM_SETTINGS_PORT 5559 listener (or `UniKostyl/rpi_cam_ctl.py`). Without a Pi restart/deploy, laptop CAM SET trackbars cannot change Pi camera settings.
- T3: C++ `pi_core/src/gate_cluster.{hpp,cpp}` was direct-built with MinGW/WinLibs; `pi_core_tests_direct.exe` passed `checks=540 failures=0`, and `Python-vs-C++ parity_check5` passed on 61 frames / sim-red-folder. Full OpenCV/ZMQ `pi_core` binary not built.
- T2/T4: title-bar X close paths are mock-tested via cv2 visibility/error; they were not manually clicked on the live Windows desktop.
- T6: live smoke captured `out/live_smoke_final2.png` (contains user/room; do not publish without consent). INFO/HSV interaction and title-bar X click still need manual verification.

## Notes for other agents (from finished tasks)

### Idea 1 (done) — camera settings: cam_settings.py API, port 5559 topology, camera_settings.json

- **Contract file** `UniKostyl/camera_settings.json` (laptop) / `/home/twox/camera_settings.json` (Pi):
  `{"version":1,"ts":"...","controls":{"exposure":300,"gain":0,"brightness":0,"gamma":100,"saturation":60,"contrast":32,"power_line_frequency":1,"exposure_dynamic_framerate":0}}`.
  Values are ALWAYS in V4L2/UVC units (exposure = exposure_time_absolute, 100 µs)
  so one file serves both backends. **Unknown top-level sections are preserved on
  write** (atomic: same-dir tmp + fsync + os.replace) — Idea 8's `self_mask`
  section can live in this file; edit it via `cam_settings.save_settings(path,
  controls)` / read via `cam_settings.load_settings(path)` (never raises; corrupt
  -> None). Range registry: `cam_settings.CONTROLS` (sync copy in
  `rpi_elp_cam.py::_CAM_SETTINGS_RANGES` and in `rpi_cam_ctl.py`).
- **Local-camera default fix (2026-11-17, user bug):** cv_kostyl.py now
  auto-detects the ELP via `fisheye.find_full_fov_camera()` (the camera that
  delivers 1920x1080; built-in webcam cannot), fallback index 1, and opens it
  at **1920x1080 MJPG** (lower ELP modes are sensor crops). The frame is
  software-downscaled to the previous pipeline size (720x1280 after the
  existing ROTATE_90_COUNTERCLOCKWISE) — full FOV, same CPU cost.
  `unikostyl.camera_input_text` is set to the detected index at startup.
  Ideas 5/8/11: laptop-side working frame is 720 wide x 1280 tall (portrait).
- **Laptop API (`UniKostyl/cam_settings.py`):** `install(unikostyl, cap_getter,
  source_getter)` -> `CameraSettingsWindow` (call `.pump()` every frame,
  `.close()` on exit; the CAM SET button is auto-registered into
  `unikostyl.widgets/wnames` under `"btn_cam_settings"` — Idea 2's panel and
  others can position relative to it: top row, x=530, 110x35).
  `LocalCameraCtl(cap_getter).apply(controls)` -> per-control status dicts
  `{requested, readback, state, note}` with states
  `ok/unconfirmed/mismatch/n-a/nocam/fail`. `PiCamCtl` (PUB **bind** `tcp://*:5559`
  on the laptop; Pi SUB **connect** — same topology as the color SUB; message
  `{"cmd":"set","controls":{...},"ts":...}`; full set re-sent every 5 s while the
  window is open to survive SUB late-join; no ack channel).
- **Hardware-verified MSMF facts (laptop ELP index 1 @1920x1080, 2026-11-17; see
  `out/camset_check.py`):** cap.set() APPLIES gain/brightness/exposure, but
  cap.get() readback is FROZEN per property (brightness always 0.0, exposure
  always -6.0, gamma/saturation/contrast stuck at open-time values). MSMF
  interprets CAP_PROP_EXPOSURE as **log2 seconds** (DirectShow convention);
  cam_settings converts UVC 1..5000 -> log2(uvc*1e-4) automatically. Stuck
  readbacks are detected dynamically (3 distinct requests -> identical answer)
  and reported as `unconfirmed` — never silently "ok". If another idea needs
  camera control on the laptop, use `LocalCameraCtl`, do not raw-call cap.set.
- **Pi side:** `UniKostyl/rpi_cam_ctl.py` — standalone mini-service (only
  python3-zmq + v4l-utils), SUB connect `tcp://<laptop-ip>:5559`, applies via
  v4l2-ctl batch with per-control retry (same pattern as `v4l2_set`), clamps to
  verified ranges, drops unknown controls, forces `auto_exposure=1` (AE must
  never be enabled), tolerates malformed JSON. Deploy:
  `pscp -batch -pw <pw> UniKostyl/rpi_cam_ctl.py twox@<pi>:/home/twox/`
  then `plink -ssh -batch -pw <pw> twox@<pi> "python3 rpi_cam_ctl.py --laptop-ip <laptop>"`.
  `rpi_elp_cam.py` (and later pi_core) applies `camera_settings.json` AFTER CLI
  flags at startup (`apply_camera_settings_file` in rpi_elp_cam.py; missing file
  -> CLI values only). pi_core seam (from Idea 4 notes): add a 5559 SUB in C++
  or keep rpi_cam_ctl.py running alongside — the port and message schema are fixed.


### Idea 2 (done) — status JSON schema + integration points

- **Pi -> laptop status (part 2 of multipart video on 5555):**
  `{"gates":{"1":{"enabled":true,"found":true,"angle":123,"area":456},...},"fps":25.9}`.
  Keys are strings "1".."N" (N = `--gates`, default 2). `found:false` -> no
  angle/area keys. Optional per-gate fields (`dist_cm`/`dist_reliable` —
  Idea 6, `role` — Idea 9, edge sectors — Idea 11) pass through the laptop
  parser untouched — just add them on the Pi side, the UI picks them up.
  `--no-status` on the Pi restores the old single-part JPEG. An un-upgraded
  laptop (old `recv()`) degrades gracefully: JSON part arrives as a separate
  message and is skipped as `[NET ERROR]` (video continues at ~half rate) —
  this is why Idea 4's C++ core should repeat the exact same multipart format
  (`build_gate_status` in `rpi_elp_cam.py` is the reference, pure function).
- **Laptop API (unikostyl.py):** `unikostyl.set_pi_status(parsed_dict)` /
  `set_video_source("local"|"pi", desc)` / `set_publish_state(ok, note)` /
  `note_colors_sent()` — all safe no-ops if the panel import failed.
- **Radar (Idea 7) / distance (Idea 6):** read `status_panel.parse_status_payload()`
  output (normalizes gates, keeps unknown fields, adds `ts` + `extra`) or the
  panel widget's data (`UniKostyl.widgets["status_panel"].pi_status`).
- Panel widget: `status_panel.StatusPanel(screen, rect, TRACK_COLORS)`, registered
  in `unikostyl.widgets` under `"status_panel"` only if `import status_panel`
  succeeds; display-only (mousedown passes through).

### Idea 4 (done) — C++ core layout, APIs and next steps

- **Everything lives in `UniKostyl/pi_core/`** (only this dir was touched):
  `src/` (14 modules), `tests/`, `vendor/` (nlohmann/json v3.12.0 + cppzmq v4.11.0
  single headers), `CMakeLists.txt`, `deploy.sh` (pscp+plink build/smoke, needs
  `PI_PW`). Build on the Pi: `sudo apt install -y build-essential cmake
  libopencv-dev libzmq3-dev`, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  && cmake --build build -j 3`. `rpi_elp_cam.py` stays untouched = rollback path.
- **Pure-logic layer (no OpenCV/ZMQ, unit-tested on Windows):**
  `hsv_color.hpp` (rgb_to_hsv_8u, bit-exact vs cv2.COLOR_RGB2HSV — validated on
  200k random px + 2029 parity vectors, 0 mismatches), `gate_logic.{hpp,cpp}`
  (hue-wrap ranges, gate_angle, wrap-aware angle diff, serial decision policy,
  contract line formatters), `gate_store.{hpp,cpp}` (saved_gates.json Idea 3
  schema, atomic write, role-field passthrough for Idea 9),
  `color_message.{hpp,cpp}` (colors JSON parse incl. legacy `{"rgb"}` and
  `{"stream"}`), `atomic_file.{hpp,cpp}`, `config.{hpp,cpp}` (all
  `rpi_elp_cam.py` flags + `--print-serial`), `log.{hpp,cpp}`.
- **IO layer (compiles only on the Pi):** `gate_tracker.{hpp,cpp}` (inRange +
  OPEN/CLOSE + moments per gate, mutex-guarded params), `camera.{hpp,cpp}`
  (RawCamera + CaptureThread), `v4l2_ctl.{hpp,cpp}` (batch + per-control retry),
  `serial_link.{hpp,cpp}` (termios or stdout sink), `streamer.{hpp,cpp}` (ZMQ PUB
  5555), `color_listener.{hpp,cpp}` (ZMQ SUB connect 5557), `main.cpp`.
- **APIs for Ideas 5–8:** `GateTracker::process(hsv, idx, GateResult&)` returns
  `{cx,cy,angle,area,area_px}` — Idea 5 clustering/hysteresis wraps the
  found/lost input of `gate_logic::decide_serial` (debounce filter) and/or
  replaces the centroid source; Idea 6 adds fields to `GateResult`; Idea 8 zeros
  the exclusion zone in the hsv/mask Mats before `process()`. Ideas 6/9 extra
  fields in the Idea 2 status JSON pass through — add them on the Pi side in
  `main.cpp`'s status plumbing when Idea 2's multipart format is repeated here.
- **Known next steps for pi_core:** (1) repeat Idea 2's multipart
  `[JPEG, JSON status]` on 5555 — reference `build_gate_status` in
  `rpi_elp_cam.py` (pure function), seam reserved in `streamer.hpp`; (2) Pi-side
  `camera_settings.json` auto-apply from Idea 1 (5559 SUB is reserved, not
  implemented); (3) Ideas 5–8 logic parity.
- **Deliberate deviations from Python (documented in code):** wrap-aware
  angle-diff threshold (359→1 = 2°, not 358 — PROMPT 2.3; Python over-sends near
  0°), RGB clamping instead of uint8 wrap for out-of-range colors, throttled
  (2 s) coalesced `saved_gates.json` writes on the Pi + flush on exit, serial
  logs to stderr and `--print-serial` lines to stdout (grep-friendly).
- **Local verification:** `python -m ziglang c++` (pip ziglang, dev-only tool)
  compiles the logic layer on Windows; `pi_core_tests` = 405 checks / 0
  failures, `--vectors` parity = 2029 vectors / 0 mismatches. Pi-only IO files
  are marked `// UNVERIFIED ON HW:`

### Idea 3 (done) — saved_gates.json schema + gate_store API (Ideas 9/10 extend this)

- **Contract file** `UniKostyl/saved_gates.json` (laptop) / `/home/twox/saved_gates.json`
  (Pi — gate_store.py deploys next to rpi_elp_cam.py; `GS.default_path()` = dir of
  the module itself). Schema (every gate record ALWAYS has all 4 keys):
  `{"version": 1, "gates": {"1": {"rgb": [r,g,b], "lab": [Lmin,Lmax,Amin,Amax,Bmin,Bmax]|null,
  "enabled": true, "ts": "2026-11-17T19:00:00"}, "2": {...}, "3": {...}}}`.
  **Unknown per-gate fields are preserved on rewrite** (upsert is copy-on-write):
  Idea 9 just writes `rec["role"]="ours"|"opponent"|"unset"` via
  `gate_store.upsert(gates, id, rgb=None, lab=None, enabled=...)` + save; Idea 10
  can add its own keys the same way. Slots 4–5 (scratch) are NOT persisted.
- **API (`UniKostyl/gate_store.py`, stdlib-only, headless-safe, Pi-safe):**
  `load_gates(path)` (corrupt/wrong-version -> `{}`, never raises),
  `save_gates(path, gates)` (atomic: same-dir tmp+pid, fsync, `os.replace`,
  stale-tmp cleanup; False on failure, old file intact),
  `upsert(gates, id, rgb=None, lab=None, enabled=True, ts=None)` (lab=None keeps
  saved LAB — the Pi uses this; ids outside 1..3 ignored+warned),
  `iter_enabled_rgb(gates)`, `lab_configured(lab)`, `clamp_lab(lab)`,
  `same_rgb(...)`, and `GateStore(path, min_interval)` with `restore_rgb()`,
  `set_rgb(id, rgb, lab=None)`, `get_lab(id)`, `flush_if_due()`, `flush_pending()`.
  Laptop uses `GateStore()` (min_interval=0 → write on every SAVE, per PROMPT
  "immediate atomic overwrite"); Pi uses `min_interval=1.0` + per-frame
  `flush_if_due()` + `flush_pending()` at exit (duplicate colors never rewrite).
- **Integration seams (all additive, no behavior change for other ideas):**
  cv_kostyl.py — startup restore block after the 5557 bind (restores
  `unikostyl.thresholds[0..2]` from `lab` + `set_thr_to_sliders()` — REQUIRED,
  main_loop_frame writes slider values back into `thresholds[thr_index]` every
  frame and would wipe restored slot 1 otherwise); periodic re-send of restored
  colors for 10 s after startup and re-armed on every CONNECT to a Pi (ZMQ PUB
  drops messages sent before the Pi's SUB connects); SAVE branch persists via
  `set_rgb(..., lab=unikostyl.thresholds[gid-1])`. rpi_elp_cam.py — `run()`
  restores gates right after `GateTracker` creation (RGB only, gated by
  `--gates`, WARN for ids above gate_count); `RGBListener` persists colors
  received on 5557; main loop calls `flush_if_due()`. Idea 4's C++ core repeats
  the same schema in `pi_core/src/gate_store.{hpp,cpp}` (role passthrough).
- **Verified PC-only:** `out/gate_store_check.py` (47 checks: round-trip,
  atomic-write under simulated crash, corrupt-file tolerance, Pi-restart
  restore == live-color parity incl. hue wrap, RGBListener over real ZMQ,
  unikostyl restore under SDL dummy, cost < 0.01 ms/frame). NOT verified on
  hardware: Pi restart without laptop, live camera round-trip.
- Deploy note: `pscp -pw <pw> UniKostyl/gate_store.py UniKostyl/rpi_elp_cam.py
  twox@<pi>:/home/twox/` (and optionally the laptop's saved_gates.json for the
  initial state — afterwards the Pi maintains its own copy)..

### Idea 5 (done) — blob clustering + loss hysteresis: gate_cluster.py / pi_core APIs, defaults, Idea 6 hooks

- **NEW module `UniKostyl/gate_cluster.py`** (deploys next to rpi_elp_cam.py,
  like gate_store.py — `pscp ... gate_cluster.py`): the whole Idea 5 logic in
  one place, reused by BOTH rpi_elp_cam.py and unikostyl.py. API:
  `resolve_params(w, h, merge_gap_px=None, min_area_px=None, max_stray_px=None,
  open_kernel, lost_frames, confirm_frames)` (None/<=0 -> width-scaled
  defaults), `detect_gate(mask, params, prev_x=None, prev_y=None, labels=None,
  want_members=False)` (cv2 level: countNonZero quick reject +
  connectedComponentsWithStats + pure part), `build_gate_object(comps, params,
  prev_x, prev_y, want_members)` (PURE, mirrors C++ 1:1; comps = (cx, cy,
  area, x, y, w, h) tuples from CC stats), `angle_deg(cx, cy, cx0, cy0)`,
  `GateStability(lost_frames, confirm_frames, jump_px).update(det_or_None)` ->
  published dict or None (+ `.lost_event`, `.held_xy()`, `.reset()`).
- **Published det dict:** `{cx, cy (float, exact area-weighted centroid),
  area (px), x, y, w, h (union box), radius, n_clusters, n_components,
  [held: True]}`; `members: [(cx, cy, area), ...]` only when
  `want_members` (unikostyl RING mode uses them for the circular-mean angle).
- **Hysteresis semantics (both sides identical):** loss after N=5 consecutive
  empty frames (lost_frames) -> published None (handle_serial then sends
  `G<id>,-1,0` exactly once — serial format unchanged, only the trigger moved);
  grace window (<N) publishes the last accepted position with `held: True`;
  a new detection is accepted after M=3 consecutive frames (confirm_frames);
  while tracking, the cluster nearest the previous position wins in-frame and
  a far new object takes over after M stable frames WITHOUT an intermediate
  loss blip; a flickering far object never confirms -> honest loss at N.
  EXCEPTION: the very first detection of a fresh tracker is accepted
  immediately (legacy behavior — keeps out/status_check.py and
  out/gate_store_check.py green and boot latency at zero).
- **Parameter defaults chosen (resolution-scaled from track width):**
  merge_gap = 2.5% of width (16 px @640, spec "~2-3%"), max_stray = 10% of
  width (64 px @640) — max_stray doubles as the track-continuity radius
  (one spatial scale of "same object"), min_area = AREA_MIN_PIXELS_1080P=100
  (1080p-equivalent, scaled like area_min; laptop uses TARGET_MIN_AREA=300),
  open_kernel = 3 (odd, 1 = OPEN off), lost_frames = 5, confirm_frames = 3.
  CLI: `--merge-gap --min-area --max-stray --lost-frames --confirm-frames
  --open-kernel` on rpi_elp_cam.py AND pi_core (same defaults; 0/<=0 = auto).
- **radius definition (IMPORTANT for Idea 6):** radius = circumcircle of the
  merged cluster's union box, `0.5*sqrt(w^2+h^2)` — a deterministic upper
  bound of pixel-level minEnclosingCircle (exact for compact/rectangular
  blobs, ~sqrt(2)/2 vs 0.5 diameter for round ones). Chosen for bit-exact
  Python/C++ parity and frame-to-frame stability; Idea 6's calibration fits
  a,b,c to whatever consistent radius this is. GateResult/pi status JSON now
  carry `radius` (px) and `w_px` (cluster width; usable for the distance
  model) — additive fields, schema unchanged.
- **pi_core:** pure layer `src/gate_cluster.{hpp,cpp}` (no OpenCV;
  ClusterParams/GateComponent/GateObject/build_gate_object/GateStability —
  merge decisions compare SQUARED distances exactly like the Python numpy
  pair matrix; stray/stability use sqrt; accumulation orders mirrored) —
  unit tests in tests/test_logic.cpp (453+ checks incl. Idea 9's) and the
  parity mode `pi_core_tests --cluster-parity FILE` vs
  `out/parity_check5.py` (bit-for-bit over CC-derived vectors, 4 seeds OK).
  IO wiring in `gate_tracker.cpp` process(): inRange -> OPEN(configurable) ->
  CLOSE(3x3) -> CC -> build_gate_object -> GateStability; GateResult gained
  `radius/w_px/held`; `update_gate_from_rgb` resets hysteresis on a real
  color change (same in Python). config.{hpp,cpp}: 6 new flags + usage.
- **Laptop (unikostyl.py):** compute_track's findContours/boundingRect block
  replaced by gate_cluster.detect_gate + per-slot GateStability
  (`_track_stability`, `_track_last_thr`, `_track_labels` in unikostyl;
  params in `unikostyl.CLUSTER_PARAMS` dict). rec gained `radius` and
  optional `held`; w_px is now the merged cluster width (was largest-contour
  bbox width — slightly wider when the opponent splits the gate, more honest
  for the distance model). RING-mode angle = area-weighted circular mean of
  member component centroids (was contour-point mean; differs by ~1 deg).
  A slot threshold change resets that slot's hysteresis. Any frame size works
  (Idea 1's portrait 720x1280 included — params scale from width).
- **Cost measured (PC, 640x360, 3 gates):** tracker.process total 10.4 ms/frame;
  the clustering sub-stage (CC + pure logic, printed as `ворота X (кластер Y)`
  in the FPS log) is 1.82 ms/gate and REPLACED the moments pass (~1.7 ms/gate)
  — net addition ~0.1-0.4 ms/frame. countNonZero used as the quick reject is
  NOT the forbidden findNonZero (plain counter, no allocation).
- **NOT verified on HW:** Pi deploy of gate_cluster.py + rpi_elp_cam.py, live
  FPS/cluster timing on the Pi (Pi offline per PLAN.md), live-camera UI smoke
  of the laptop path (camera-free SDL-dummy coverage exists; cv_kostyl itself
  untouched by Idea 5). Serial -1-once-after-N verified against a stub link in
  out/cluster_check.py, not against an Arduino.

### Idea 9 (done) — gate roles: role semantics, APIs and integration points

- **Semantics (single source of truth — `UniKostyl/gate_store.py`):** a role is
  `"ours"` / `"opponent"` / `"unset"`, and **"unset" == absence of the `role`
  key** everywhere (file, wire 5557, status JSON 5555). `gate_store.ROLES`,
  `normalize_role(v)` (None/junk -> "unset"), `stored_role(rec)`,
  `roles_map(gates)` -> `{int_id: "ours"|"opponent"}` (set roles only),
  `GateStore.get_role(id)` / `GateStore.roles()` / `GateStore.set_role(id,
  role)` (immediate atomic write on the laptop store; keeps rgb/lab/enabled).
  `upsert(..., role=None)` — None preserves the previous role, "ours"/
  "opponent" set it, "unset" removes the key; unknown fields (Idea 10) still
  survive. `same_rgb(..., role=None)` and `GateStore.set_rgb(..., role=None)`
  treat identical color+role as a duplicate (no file rewrite).
- **saved_gates.json:** role lives in the slot record next to rgb/lab/enabled
  (only when set). A role on a slot with no color is session-only (loader
  drops records without rgb AND lab — by Idea 3 schema).
- **Wire 5557 (additive):** `{"gates":[{"id":N,"rgb":[...],"role":"ours"}]}`
  — the `role` field is attached by `cv_kostyl.build_gate_payload(pending,
  roles)` ONLY when set (never "unset"); without roles the payload is
  byte-shape-identical to the Idea 3 contract. Sent on SAVE and in the
  startup auto-send. **Live un-set is not possible by design:** the wire
  never carries "unset", a message WITHOUT role keeps the previous role
  (old-laptop safe) — to clear a role on the Pi, unset it in CAM SET and
  restart the Pi (its saved_gates copy drops the key on the next real color
  update; the laptop's copy drops it immediately).
- **Pi side (rpi_elp_cam.py):** RGBListener normalizes `item["role"]`
  (only "ours"/"opponent" accepted), stores it via `set_rgb(role=...)` and
  puts it into `tracker.params[i]["role"]` AFTER `update_gate_from_rgb`
  (that call replaces the params dict — role would be lost otherwise;
  `_effective_role()` falls back to the stored copy so old-laptop color
  updates don't clear the role in the status). `run()` restores roles from
  saved_gates.json at startup. `build_gate_status` adds `"role"` per gate
  only when set (additive; verified old-parser-safe in out/status_check
  semantics). Tracker hot path untouched.
- **pi_core C++:** `gate_logic.hpp` — `enum class GateRole {Unset,Ours,
  Opponent}` + `gate_role_from_string()` + `GateParams.role` (POD, hot path
  ignores it); `color_message` parses optional per-gate `role` into
  `gate_roles[i]` (1:1 with `gates[i]`, empty = not set); `color_listener`
  calls `tracker.set_gate_role(idx, ...)` (new GateTracker method, mutex-
  guarded metadata) + `store.set_gate_from_network(id, rgb, role)`;
  `main.cpp` restores the role from saved_gates.json at startup. When
  Idea 2's multipart status is repeated in C++, report `GateRole` from the
  params ("role" key only when Ours/Opponent) to match the Python JSON.
- **Laptop UI:** CAM SET window (cam_settings.py) gained a GATE ROLES
  section (canvas buttons per slot: ours green / opponent red / unset grey,
  9 clickable rects `role<gid>_<role>`), canvas grown to 660x448. `install()
  ` new kwargs: `gate_store_obj` (the cv_kostyl GateStore — single file
  owner), `roles_changed_cb`, `gate_colors` (auto-taken from
  `unikostyl.TRACK_COLORS`). A role click writes saved_gates.json
  immediately and fires the callback; cv_kostyl pushes roles to the panel
  via `status_panel.StatusPanel.set_roles({int_id: role})`. Status panel
  badges: «НАШИ» green / «ПРОТИВНИК» red at the row's right edge — Pi rows
  read `role` from the status JSON, local rows from `set_roles` (fed at
  startup and on every role click). Nothing is drawn on the video frame.
- **Verification (PC):** `python out/role_check.py` — 62 checks (units,
  file round-trip incl. restart, wire additive byte-shape, RGBListener over
  real ZMQ incl. old-laptop/garbage/duplicate cases, Pi-restart restore,
  status JSON shape, panel badges under SDL dummy, CAM SET headless canvas
  + install); `out/status_check.py` and `out/gate_store_check.py` stay
  green; `python -m ziglang c++` builds pi_core logic layer, tests 466/0.
  Preview images: `out/role_check_tmp/panel_roles.png`, `camset_roles.png`.
- **NOT verified on HW:** role over the real 5557/5555 links, Pi restart
  with roles, badges against a live Pi stream (Pi offline — deploy
  `gate_store.py` + `rpi_elp_cam.py` together). C++ IO files (color_listener/
  gate_tracker/main) compile only on the Pi. Serial/Arduino untouched (roles
  intentionally do NOT reach the Arduino per section 4).

### Idea 8 (done) — robot self-mask: APIs, defaults and follow-up hooks

- **NEW module `UniKostyl/self_mask.py`** (deploys next to rpi_elp_cam.py —
  ADD `self_mask.py` to the pscp deploy list, already noted in
  UniKostyl/AGENTS.md). API: `resolve_params(w, h, cx, cy, r, r2, enabled)`
  (CLI semantics: None/<=0 = auto: center, r=0.22*min(w,h)), `make_params`
  (raw px, annulus r2<=r collapses to disk), `params_from_fractions(w, h,
  frac)` (camera_settings.json section -> px), `parse_section` /
  `load_section(path)` (never raises; corrupt -> warn), `excluded(params, x,
  y)` (pure point test), `build_keep_mask(params, w, h, out=None)` (uint8
  255=keep / 0=body, ONE-TIME build, bit-exact with C++), `apply_keep(keep,
  mask)` (the only per-frame op: one cv2.bitwise_and), `status_dict(params)`
  (ints px for the status JSON).
- **Schema** `"self_mask"` in camera_settings.json (FRACTIONS — one file
  serves Pi 640x360 and laptop 720x1280 portrait):
  `{"enabled": true, "cx": 0.5, "cy": 0.5, "r": 0.22, "r2": 0.0}`
  (r2 = optional annulus outer radius; searchable ring is r..r2, per PROMPT
  §6 "between detected, inside/outside excluded"; reference annulus
  r 110..355 px ≈ fractions 0.15/0.49 at min-dim 720). The section OVERRIDES
  CLI flags on the Pi (applied after them, same order as Idea 1 controls).
  cam_settings.save_settings preserves the section (verified).
- **Semantics:** the zone is zeroed in each per-gate/per-slot MASK right
  after inRange, BEFORE morphology/CC (rpi_elp_cam.GateTracker.process,
  unikostyl.compute_track, pi_core gate_tracker.cpp process). Zeroing
  HSV/BGR instead is UNSOUND (ranges with v_lo=0/s_lo=0/H-wrap match black
  pixels — the carbon tubes would leak through; regression-covered in
  out/selfmask_check.py). Hysteresis/serial behavior unchanged (held
  positions were accepted under the mask).
- **Status JSON (Idea 2), additive:** top-level
  `"self_mask": {"cx","cy","r","r2"}` (px of the working frame, only when
  enabled) from rpi_elp_cam.build_gate_status; the laptop parser keeps it in
  `extra` (verified). pi_core: `tracker.self_mask()` returns the applied
  SelfMaskParams — report the same key when Idea 2's multipart status is
  repeated in C++ (comment left in main.cpp).
- **Laptop API for live updates (used by Idea 7 radar / future CAM SET):**
  `unikostyl.SELF_MASK` is the fractions dict (module global, loaded from
  camera_settings.json at import); `unikostyl.set_self_mask_fractions(frac)`
  updates it live and invalidates the keep-mask cache (rebuilt next frame).
  RING mode (fisheye_mode==2) never applies the disk mask (strip geometry —
  "cut center %" owns the body there). ZONE_RADIUS_FRAC is a different
  entity — do not mix.
- **CAM SET window integration deliberately deferred** (per task; regions in
  cam_settings.py were free but a live trackbar needs frame-size plumbing
  for fractions<->px and the Pi has NO live channel for self_mask — it
  applies the section at startup only, so live trackbars would desync
  laptop vs Pi). Follow-up pass should hook: (1) trackbars write
  `unikostyl.set_self_mask_fractions(...)` for the laptop preview;
  (2) persist via the `self_mask` section of camera_settings.json
  (extend cam_settings.save_settings with an extra_top={"self_mask": ...}
  or direct JSON edit — save_settings already preserves it); (3) live Pi
  push needs an additive `{"cmd":"self_mask", ...}` message on 5559 +
  rpi_cam_ctl.py handling + a runtime set_self_mask in rpi_elp_cam.py /
  pi_core (GateTracker.set_self_mask is thread-safe and can be called at
  any time). Live zone preview on the frame: draw the keep-mask border
  (cv2.Canny of the keep mask) — see out/selfmask_preview.py for the exact
  rendering recipe (project rule: nothing drawn on the real UI frame —
  render into a settings-window canvas instead).
- **Cost measured (PC):** one cv2.bitwise_and = 0.026 ms/gate at 640x360
  (~0.08 ms/frame for 3 gates); end-to-end GateTracker.process delta
  +0.072 ms/frame (min-of-3, 200 frames); keep-mask build 1.9 ms once at
  640x360, 7.5 ms once at 720x1280. C++: same equation, build_keep_mask in
  the pure layer, zero-out is one cv::bitwise_and per gate.
- **Verified PC-only:** out/selfmask_check.py (sections A-H: units, Pi path
  incl. the HSV-zero trap regression + fresh-tracker first-frame leak, laptop
  path under SDL dummy incl. live set_self_mask_fractions + RING exemption,
  status JSON additive key + parse_status_payload extra, camera_settings
  round-trip via cam_settings.save_settings, C++ keep-mask parity bit-exact
  on 5 cases + 50 probes, cost, --help flags). pi_core_tests 501/0 (35 new
  checks + --selfmask-parity mode). Regressions green: cluster_check (its
  section E now disables the default self-mask — the Idea 5 blobs graze the
  zone; restored after), status_check, gate_store_check, mask_check,
  role_check, parity_check5, camset_check. Static scene check:
  out/selfmask_preview.py — default zone covers 100% of the Pi PCB on
  out/scene_red_folder.png (8.6% of the frame excluded).
- **NOT verified on HW:** Pi deploy (rpi_elp_cam.py + self_mask.py +
  camera_settings.json) and live FPS/log on the Pi (offline); the C++
  gate_tracker/main wiring compiles only on the Pi (pure layer unit-tested;
  UNVERIFIED ON HW markers in gate_tracker.cpp); live cv_kostyl UI smoke
  not run (GUI app — camera-free compute_track coverage exists instead).

### Idea 7 (done) — radar panel («кружочек»): radar_panel.py API, integration seams, Idea 6/10 hooks

- **NEW module `UniKostyl/radar_panel.py`** (laptop-only, like status_panel.py —
  NOT deployed to the Pi): `RadarPanel(Widget)` — dark instrument panel with a
  black disc, thin rim, 30° ticks + cardinal labels (all STATIC, cached in a
  Surface rebuilt only on resize/body-zone change), gate arcs of TRACK_COLORS
  at their angles near the rim (span ±10°, width ~R/14), thin green/red role
  badge arcs (Idea 9) just inside the gate arcs, and the target marker: white
  circle + line from the center + "angle°" label (auto dark-on-light over the
  body zone) + a footer line `G2  120°  r = 263 px  -> 48 cm`. Display-only
  (mousedown passes through), hidden by DEFAULT — the RADAR button toggles it
  (button label turns green while on).
- **Data flow (consistency rule kept):** angles are CONSUMED as-is, never
  recomputed (no atan2 in the module; RING-mode angles from compute_track's
  strip semantics work unchanged). Pi mode: `radar.sync_from_status_panel(
  unikostyl.widgets["status_panel"])` copies `pi_status` BY REFERENCE (single
  parse in status_panel), `source_kind` and `roles`; fresh-stale selection
  mirrors status_panel exactly (`status_panel.PI_STATUS_STALE_S` = 2 s; stale
  header tag "stale", falls back to local results). Local mode:
  `radar.set_local_results(track_results)` — the same feed call site as the
  status panel (unikostyl.main_loop_frame section 5c). Feeds also include
  `set_target_gate(TRACK_SLOTS[target_slot]+1)`, `set_body_zone(SELF_MASK)`
  (Idea 8: body circle = r/0.5 of disc radius — rim == min(w,h)/2; RING
  excluded, same as _self_keep_mask; r2 draws the search-boundary ring) and
  `set_frame_size(w,h)` (r_px marker scaling). Hidden radar: draw() is a
  no-op, the 5 setters cost ~0.003 ms/frame total (measured).
- **Idea 6 hook — dist_cm appears on the radar AUTOMATICALLY:** pi gates are
  read from the normalized status dict (`dist_cm`, `dist_reliable` — shown
  only when reliable, missing key = reliable, same semantics as the status
  panel), local recs read `rec["dist_cm"]` + `rec["dist_reliable"]` (already
  tolerated before Idea 6 lands). When the fields appear: footer switches
  from "r = 263 px -> —" to "r = 263 px -> 48 cm" and the marker radius
  scales by dist_cm/RADAR_MAX_CM (250, clamped 0.30..0.92; r_px/(min(w,h)/2)
  when no dist; fixed 0.72 for pi without dist). A pi-side `r_px` field would
  also be picked up automatically (read via _safe_float). Idea 10: `held`
  gates (Idea 5 hysteresis) draw DIMMED arcs ("est"-like); "last known"
  directions from field_map.json can later be injected by extending
  `_gate_data()` (angles consumed as-is) — no radar rework needed.
- **Integration points in unikostyl.py (pinpoint, all guarded by
  `_radar_panel is not None`):** import near the status_panel import;
  `toggle_radar()` next to `set_pause()`; `widgets["radar_panel"]` +
  `widgets["btn_radar"]` registered after the status_panel block (before
  `wnames = list(...)`); rect in `update_all_widgets()` — right column under
  the status panel (`rp_y = sp_y+sp_h+6`, 250 wide, height clamp 96..260;
  at 1280x720 it is 250x99 — a small but working dial, maximize the window
  for the full 260 px radar); RADAR button at **x=650** (top row, right of
  CAM SET at 530..640) — a follow-up pass may move it into the CAM SET
  canvas (cam_settings.py, additive; cv_kostyl.py was NOT touched — roles
  reach the radar via the status panel widget).
- **Font glyph facts (verified by metrics in out/radar_check.py):** the
  bundled pygame font (freesansbold) has NO U+2192 "→" (renders as tofu) —
  rendered labels use ASCII "->"; U+2014 "—" and U+00B0 "°" are fine.
- **Verification (PC, camera-free):** `python out/radar_check.py` — 77 checks:
  registration/toggle, data units (_gate_data pi+local, _target_line formats,
  _marker_frac priorities, set_body_zone mapping, set_roles unset-invariant,
  sync identity), PIXEL tests on the rendered radar (arcs at 10°/120° incl.
  358° wrap, badges ours-green/opponent-red/absent, marker circle+line at the
  selected gate, held dimmed, stale>2 s switches to local arcs+footer), the
  main_loop_frame feed + layout/no-overlap + resize growth, and the cost
  gates (< 0.5 ms visible — measured 0.09; ~0 hidden — 0.0002; feed 0.003).
  Regressions green: status_check (its A/B timing check is known-flaky — one
  noise FAIL at +4.11 ms, two clean re-runs PASS at -1.4/-0.9 ms), gate_store
  _check, role_check (incl. CAM SET co-install with the RADAR button),
  cluster_check. `python -m py_compile` on all touched files.
- **NOT verified on HW:** live Pi multipart stream feeding the radar (Pi
  offline per PLAN.md — the radar's Pi path is verified only with synthetic
  parsed statuses over SDL dummy), live-camera view + real mouse interaction
  (needs a human at the laptop display), and the body-zone visual match
  against the real self-mask circle on live frames. Marked `# UNVERIFIED ON
  HW:` in radar_panel.py header.

### Idea 6 (done) — gate distance: distance.py / pi_core APIs, distance_calib.json schema, integration points

- **NEW module `UniKostyl/distance.py`** (deploys next to rpi_elp_cam.py —
  ADD `distance.py` to the pscp deploy list, already noted in
  UniKostyl/AGENTS.md; numpy + stdlib only, headless-safe, no cv2/zmq/pygame).
  API: `analytic_distance_cm(w_px, frame_w, gate_width_cm)` (fallback
  Z = W*(frame_w/pi)/w_px — the legacy unikostyl shape with the RULES GATE
  width, not the ball), `fit_model(points)` ([(dist_cm, r_px), ...] ->
  (dict|None, report) — exact linearization (d+c)(b-r)=a solved by
  np.linalg.lstsq + Gauss-Newton polish; exact synthetic data recovers
  a,b,c to 1e-9), `validate_model(a,b,c,r_lo,r_hi)`, `DistModel` (evaluate
  (r_px, frame_min_dim) -> (dist_cm|None, reliable); NEVER raises/returns
  junk; scales r by min-dim when the calibration frame differs — one
  calibration serves Pi 640x360 AND laptop 720x1280 portrait),
  `load_model(path)`/`save_model_dict(path, data)` (atomic tmp+fsync+
  replace; corrupt -> None + WARN), `model_from_dict/model_to_dict`,
  `DistHold(hold_s)` and `GateDistEstimator(model, hold_s, drop_frac,
  fill_min, move_frac, gate_width_cm).update(radius, w_px, h_px, area_px,
  cx, cy, held, now, frame_min_dim, frame_w) -> (dist_cm|None, reliable)`.
- **Contract file `distance_calib.json`** (UniKostyl/ laptop,
  /home/twox/ Pi): `{"version":1,"model":"a/(b-r)-c","a":..,"b":..,"c":..,
  "points":[[dist_cm,r_px],...],"frame_w":640,"frame_h":360,"ts":"..."}`.
  Written ONLY by the calibrator (atomic); deliberately NOT committed —
  real calibration is user-dependent on the venue. Load-time validation:
  finite a/b/c, a<0 (strictly decreasing d(r)), asymptote b outside the
  calibration r-span, monotone/finite on the whole working interval
  (span ±25%, CLIPPED to the pole-safe side of b — evaluation can never
  jump across the asymptote); anything wrong -> None -> analytic fallback.
- **Garbage protection (per frame, both languages identical):** r finite
  and >0; |b-r| > 1e-6 (division guard); r inside the working interval
  (outside -> NO value at all, not a clamped junk number); d clamped to
  0..500 cm (outside -> clamped value with reliable=false); NaN -> no
  value. Occlusion heuristics -> unreliable + HOLD the last reliable value
  for T seconds (default 2.0, `--dist-hold`): held frame of the Idea 5
  hysteresis; sudden radius drop below 0.6x reference with a stationary
  centroid (`--dist-drop`, 0=off; moved centroid = gates really moved, not
  occlusion); box fill area/(w*h) < 0.25 (`--dist-fill`, 0=off; gate pieces
  merged across the opponent's body). After T expires the dist fields are
  OMITTED from the status (stale value honestly hidden). Junk radius
  inside the hold window keeps the held value (grace).
- **Status JSON (Idea 2), additive, verified additive vs status_check:**
  found gates only: `"dist_cm": 148.2` (rounded 0.1) + `"dist_reliable":
  true|false`; lost gates / expired hold -> fields absent. Per-second log:
  `G1=123° d=148см` (`*` suffix = unreliable/held). The serial contract
  G<id>,angle,area is UNCHANGED (dist never reaches the Arduino).
  status_panel and radar_panel already render dist only when
  dist_reliable — no changes needed there (verified in dist_check D +
  radar_check).
- **rpi_elp_cam.py wiring:** `distance` imported via try/except (missing
  module = pre-Idea-6 behavior); `run()` loads distance_calib.json next to
  the script (`--dist-calib` overrides) BEFORE the tracker and passes it to
  the GateTracker ctor; per-gate `GateDistEstimator` state lives in the
  tracker (reset on a real color change, like the Idea 5 hysteresis);
  `GateTracker.process` computes dist after the hysteresis publish (one
  formula eval, ~0.004 ms/gate); `build_gate_status` adds the fields.
  New CLI: `--dist-calib --dist-hold --dist-drop --dist-fill
  --gate-width --no-dist`.
- **Laptop (unikostyl.py):** `_DIST_MODEL` loaded once at import from
  UniKostyl/distance_calib.json (missing -> analytic); per-slot estimators
  `_dist_est` created lazily in compute_track (reset together with the
  Idea 5 hysteresis on a slot threshold change). Every found slot's rec
  gains `dist_cm` + `dist_reliable` (radar/footer pick them up
  automatically); the target readout/log panel append "(удержано)" when
  unreliable. The legacy per-target ball-width estimate
  (KNOWN_TARGET_WIDTH_CM / FOCAL_DIVISOR) is superseded — the constants
  remain but are unused; GATE_WIDTH_CM=45 is the fallback width parameter.
  RING mode (fisheye_mode==2) keeps dist_cm=None (strip sizes are not
  physical) — verified in dist_check E.
- **pi_core C++ (parity, pure layer unit-tested on Windows):**
  `src/distance.{hpp,cpp}` — DistanceModel/DistEstimator/eval_distance/
  fit_distance_model/analytic_distance_cm + JSON load/save via vendored
  nlohmann + atomic_file (mirrors distance.py 1:1; fit = normal equations
  + Gaussian elimination + GN polish — same linearization, so fits agree
  to ~1e-9 on exact data but are NOT bit-identical to numpy lstsq; EVAL of
  the SAME a,b,c IS bit-for-bit). `GateResult` gained `dist_cm` (NaN =
  nothing to report) + `dist_reliable`; `GateTracker::set_distance_model()`
  + per-gate DistEstimator; config gained the same 6 flags; main.cpp loads
  /home/twox/distance_calib.json (`--dist-calib`) and logs the model or
  the fallback. When Idea 2's multipart status is repeated in C++, report
  `info.dist_cm`/`info.dist_reliable` exactly like
  rpi_elp_cam.build_gate_status (comment left in gate_tracker.hpp).
  CMakeLists: distance.cpp added to BOTH pi_core and pi_core_tests.
- **Calibrator `UniKostyl/out/dist_calibrate.py`** (camera-free capable):
  `--add СМ,ПХ` manual points (r declared in `--frame-size` scale, default
  640x360; auto-rescaled to the session scale), `--image кадр.png --dist СМ
  [--rgb R,G,B | --gate N]` measures r on a saved frame through the SAME
  runtime pipeline (inRange + self-mask + morphology + gate_cluster radius
  — so the calibration fits exactly what tracking measures), `--live`
  (SPACE record / `[` `]` distance ±10 cm / f refit / q quit, `--preview`
  shows the mask), `--remove N` / `--clear` / `--fit` / `--list`
  (residual report). Points accumulate in `out/dist_calibrate_points.json`
  (`--session`); as soon as >=3 points fit+validate, the model is written
  atomically to `--out` (default distance.default_path()). Procedure:
  robot at known distances -> 4-6 points across the working range ->
  `--list` to eyeball residuals (report prints per-point residuals and the
  r working interval).
- **Verification (PC, camera-free):** `python out/dist_check.py` —
  sections A-H, 0 failed: fit round-trip, division/interval/clamp/NaN
  guards, frame scaling, analytic fallback parity (exact vs the legacy
  formula), corrupt/wrong-version/NaN JSON -> None, atomic round-trip +
  no tmp litter, occlusion hold/expiry/recovery/fill/move/reset, Pi
  GateTracker dist fields + build_gate_status additive + --no-dist +
  parse_status_payload passthrough, laptop rec + calibrated injection +
  RING exemption, calibrator add/remove/image-measure round-trip, Python↔C++
  `pi_core_tests --dist-parity` BIT-FOR-BIT (model round-trip via %.17g,
  8 eval probes, 9 estimator steps) + fresh fits agreeing to 1e-4,
  cost 0.0044 ms per estimator call. pi_core_tests 540/0 (39 new checks +
  --dist-parity mode). Regressions green: mask_check, status_check,
  gate_store_check, role_check, cluster_check, selfmask_check, camset_check,
  parity_check5, radar_check.
- **NOT verified on HW:** Pi deploy (rpi_elp_cam.py + distance.py +
  distance_calib.json), live dist_cm on the real stream, C++ IO wiring
  (gate_tracker/main compile only on the Pi — pure layer unit-tested,
  UNVERIFIED ON HW markers in distance.cpp/gate_tracker.cpp as inherited),
  and above all the REAL-FIELD calibration (the user must run
  out/dist_calibrate.py with the robot at known distances on the venue —
  the a,b,c in this repo are test fixtures, not a calibration).
- **Notes for other agents:** Idea 10 consumed dist_cm from the status/
  rec — the field_map snapshot already benefits (its agent read this file
  first). Idea 12 tactics can trust `dist_reliable=false` = "size
  compromised — use the angle only". The model file frame_w/frame_h makes
  the calibration portable across resolutions — do NOT rescale radii
  manually anywhere else.

### Idea 10+11 (done) — field binding + boundary adviser: APIs, seams for Idea 12

- **Contract file** `UniKostyl/field_map.json` (laptop) / `/home/twox/field_map.json`
  (Pi, when field_map.py deploys): `{"version":1,"bound_at":str|null,
  "robot_pos":"center"|"own_goal"|null|"manual","heading_offset_deg":float,
  "gates":{"1":{"angle_deg":0..360,"dist_cm":0..500|null,"estimated":bool},...},
  "boundary":{...Idea 11 thresholds...}}` + unknown top-level keys PRESERVED on
  rebind (copy-on-write, like gate_store). Shipped with an EMPTY binding (gates:
  {}) — valid file, holds the boundary calibration until the first BIND FIELD.
  Reads: `field_map.load_map(path)` (validated dict | None + WARN, never raises),
  `load_boundary_section(path)` (raw "boundary" dict | {}).
  Writes: `save_map(path, data)` — same-dir tmp+pid + fsync + os.replace + stale-tmp
  cleanup (same pattern as gate_store). Rebind: `build_map_data(snapshot, robot_pos,
  heading_offset_deg, prev=FIELD_MAP)` — prev carries "boundary" + unknown keys.
- **Snapshot builders (pure, testable):** `field_map.snapshot_from_results(
  track_results)` (local recs: enabled+found only; `held` -> estimated=True),
  `snapshot_from_pi_status(pi_status)` (parsed Idea 2 status: enabled+found;
  `dist_reliable=false` -> dist_cm null — a compromised size must not be bound).
- **Blind-filling seam:** `field_map.estimated_directions(data, heading_delta_deg=0.0)`
  -> `{gid: {"angle_deg","dist_cm","estimated":True}}` (ALL bound entries; the radar
  decides when to use them). `apply_heading(data, delta)` is the Idea 12 placeholder
  — NOT called anywhere (no Arduino->Pi reverse channel exists; do not invent one).
  When Idea 12 lands: accumulate heading since bound_at, pass it as
  heading_delta_deg (SIGN to be verified against the telemetry convention —
  `# UNVERIFIED ON HW` in field_map.py), feed the result to the same
  `unikostyl.set_estimated` path (no radar rework needed).
- **unikostyl integration (all pinpoint):** `FIELD_MAP` global loaded at import by
  `load_field_map_settings()` (restart restores the binding — verified); buttons
  `btn_bind` ("BIND FIELD", x=750..860) + `btn_pos` ("POS: CENTER/OWN GOAL/MANUAL"
  cycler, x=870..1000) in the top row right of RADAR; `bind_field(path=None)`
  writes the file atomically, picks the freshest source (fresh pi_status per
  PI_STATUS_STALE_S else local track_results) and refuses with no measured gates;
  `set_bind_heading(deg)` is the programmatic heading API (UI editing deferred —
  heading is inert until the reverse channel exists). Per-frame: `_estimated_directions()`
  (~0.03 ms) feeds `_radar.set_estimated(...)` + the per-second log
  (`G1=est:123°` for gates out of view); boundary step 5d runs the detector only on
  REAL, non-paused, non-RING frames.
- **Radar (Idea 7 file) extensions:** `set_estimated(est_map)` merged in `_gate_data()`
  (lost+configured -> angle/dist from the binding, `estimated:True`, drawn DIMMED
  + "(est)" in the footer; found gates always win; unconfigured never get est),
  `set_edge_sectors(flags)` — red arcs for confirmed dangerous sectors + "EDGE" tag;
  empty flags = zero draw ops (regressions radar_check unaffected).
- **Boundary module `UniKostyl/field_boundary.py`** (laptop-side now; Pi-capable,
  needs cv2/numpy): `resolve_params(raw)` (defaults + clamps + junk-tolerant, unknown
  keys ignored) over the "boundary" section of field_map.json;
  `BoundaryDetector.process(frame_rgb, board_flags=None)` — aspect-preserving
  downscale (INTER_LINEAR; INTER_AREA measured 6.5 ms vs LINEAR 0.26 ms on
  OpenCV 5.1 — do not switch back), RGB->HSV, floor = green carpet inRange
  (hue-wrap aware) OR white-line inRange (markings are floor), per-sector
  non-floor share (8..12 sectors, band inner_frac..outer_frac of min dim, defaults
  0.24..0.46 — inner skips the Idea 8 body zone) vs edge_share; hysteresis
  confirm_frames=2 / release_frames=3. **Adviser-only by construction: no serial/
  zmq imports — cannot message the Arduino.** `board_flags` IS the fusion plug-in
  point for Idea 12 (line-board states per sector; danger = board OR camera, board
  priority, `state["board_used"]` reports it). `status_extra(state)` -> additive
  top-level `{"edge_sectors":[bool...],"edge_any":bool}` for the Idea 2 status JSON
  (laptop parser keeps it in extra — verified; add it in build_gate_status/pi_core
  when the detector ships on the Pi). Sector angles use the SAME convention as
  tracking/radar (0°=right, CCW) — radar display needs no remap.
- **Costs (PC):** detector 0.89 ms/frame @720x1280, ~1.0 ms @640x360 (only on real
  frames; paused/RING = skipped); est map 0.033 ms; radar est+edge setters 0.008 ms.
- **Verified PC-only:** out/field_check.py (units, atomic round-trip incl. corrupt
  -> WARN, UI round-trip on the REAL file with pristine restore, preset cycler,
  radar est pixel tests, EDGE arc pixel tests, parse_status_payload extra, costs)
  and out/boundary_check.py (sector geometry incl. portrait 720x1280 aspect,
  hysteresis 2-3 frames both ways, no false positives on uniform carpet/shadow/
  white line, hue wrap, fusion priority, real-frame smoke on scene_red_folder.png,
  costs). Regressions green: status_check, gate_store_check, role_check,
  radar_check, selfmask_check, cluster_check (selfmask_check had two load-dependent
  timing-check flakes on the first run — clean re-run passed all).
- **NOT verified on HW / deferred (user-dependent):** BIND FIELD against a live
  camera/Pi stream (GUI + a human needed), real-field boundary calibration (the
  "boundary" defaults are TEST fixtures — must be calibrated on the actual green
  carpet before trusting the adviser; the venue is WALLED so nothing blocks),
  heading rotation accounting (blocked on Idea 12's reverse channel),
  status_panel.py "est" badge in LOST rows (follow-up, panel file untouched here),
  Pi-side detector/status wiring (rpi_elp_cam.py + pi_core — not this task's files).
