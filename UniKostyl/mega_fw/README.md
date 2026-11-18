# mega_fw — thin Arduino Mega 2560 executor firmware (Idea 12 / PROMPT_ROBOT §3)

New firmware that replaces `uart_rpi.ino`-style "everything" behavior with a pure
executor: the Raspberry Pi (`UniKostyl/robot_core/`) owns the strategy; the Mega
drives motors, streams gyro/line/catcher telemetry and pulls the trigger.

**The old pair (`rpi_elp_cam.py` + `uart_rpi.ino`) keeps working unchanged — this
firmware replaces it only after acceptance. Nothing was deleted.**

Status: **compile-only.** The firmware has never run on hardware — never treat it
as verified. Verification recipe below. The approved binary serial protocol
(PROTOCOL.md v1) is implemented on both sides and covered by PC conformance tests.

## What is implemented

| Piece | Source | File |
|---|---|---|
| Drive safety path: `MotorInvert[]` → ±255 → proportional `MotorPWM` cap → `MotorMinPWM[]` floor → H-bridges | ported 1:1 from `rcj_arduino_src/motors.cpp` | `src/motors_fw.{hpp,cpp}` |
| MPU6050 DMP bring-up + per-robot offsets + blocking drift calibration + **drift-freeze heading** (the 100 Hz timing-critical logic stays on the Mega per the split) | ported 1:1 from `rcj_arduino_src/gyro.cpp` + `hardware.cpp::setupGyro()` | `src/gyro_fw.{hpp,cpp}` |
| 16 line sensors: mux select (PORTC/PORTD) + fast ADC (500 kHz, MUX5) + settle-discard + 2-sample average — **raw values only**, no thresholds/angle | ported 1:1 from `rcj_arduino_src/line.cpp` (`readSens_`) | `src/lines_fw.{hpp,cpp}` |
| Kicker pin 18: non-blocking 40 ms pulse + 1000 ms cooldown (rcj `kick()` timing, `delay()` removed) | ported | `src/executors_fw.{hpp,cpp}` |
| Dribbler Servo pin 12: bit → 1000 µs (off) / 2000 µs (on), configurable | new (rcj left it as an empty stub) | same |
| Catcher microswitch pin 14, active LOW | ported | same |
| Status LED moved 16 → 15 (pin 16 is Serial2 TX2 now) | documented change | same |
| Pi link RX: binary `T_DRIVE`/`T_STOP` frames with CRC8, fixed offsets, resync on `0xA5`+known TYPE; bad CRC/unknown dropped whole; valid command = drive + kicker/dribbler bits + watchdog feed | APPROVED v1, **isolated seam** | `src/link_rx.{hpp,cpp}`, `src/link_frame.hpp` |
| Pi link TX: binary `T_HDG`/`T_LINE`/`T_HB` telemetry (100 Hz heading, 50 Hz lines+catcher, 2 Hz HB) | APPROVED v1, **isolated seam** | `src/link_tx.{hpp,cpp}`, `src/link_frame.hpp` |
| Watchdog: no valid command > 300 ms → wheels 0 + kicker released + dribbler off; telemetry continues | new per split | `src/watchdog_fw.{hpp,cpp}` |
| Loop scheduler: no `delay()`, DMP FIFO drained every iteration, line poll 50 Hz | new | `src/main.cpp` |

Deliberately NOT here (they live in `UniKostyl/robot_core/` on the Pi): omni
mixing, PI/PD heading laws, all tactics, line angle computation, ball Locator,
gate parsing.

## Serial protocol

**APPROVED v1 (2026-11-18), implemented:** see [`PROTOCOL.md`](PROTOCOL.md).
Fixed binary frames `SYNC 0xA5 | TYPE | SEQ | payload | CRC8`, little-endian,
CRC-8/ATM over TYPE..payload. The framing lives in `src/link_frame.hpp`
(shared CRC + RX scanner), `src/link_rx.cpp` and `src/link_tx.cpp`; nothing else
on the Mega knows the wire format.

Summary (115200 8N1, `Serial2` = pins 16/17):

- Pi → Mega: `T_DRIVE` 13 B (4 × int16 wheel PWM −255..255 pre-invert, flags
  bit0 kicker request, bit1 dribbler) up to 100 Hz; `T_STOP` 4 B. Either frame
  feeds the watchdog. Bad CRC / unknown TYPE is dropped whole and the scanner
  resyncs on the next `0xA5` + known TYPE.
- Mega → Pi: `T_HDG` 8 B ~100 Hz (int16 centidegrees −18000..18000, still flag),
  `T_LINE` 38 B 50 Hz (16 × uint16 raw ADC 0..1023 + catcher bit), `T_HB` 4 B 2 Hz.
- Watchdog 300 ms (configurable `WatchdogMs` in `src/config_fw.hpp`).

## Build (compile-only verification)

Same recipe as `rcj_arduino_src/AGENTS.md`. On this machine PlatformIO is
installed via Python and on `PATH`:

```powershell
platformio run --project-dir UniKostyl\mega_fw
```

Success = the `[SUCCESS]` line + RAM/Flash report, exit code 0.

PC conformance test for the framing (no Arduino toolchain needed):

```powershell
python -m ziglang c++ -O2 -std=c++17 -I UniKostyl/mega_fw/src `
  UniKostyl/mega_fw/tests/test_link_frame.cpp `
  -o UniKostyl/mega_fw/build/test_link_frame.exe
./UniKostyl/mega_fw/build/test_link_frame.exe
```

Green = `21 checks, 0 failures` (CRC check value, golden drive/stop frames,
back-to-back frames, garbage, bad CRC, unknown type, lost-byte resync).

The firmware cannot run on the host. **Never claim a runtime or hardware test
after only compiling.**

## Hardware notes / changes vs the reference firmware

- **Status LED 16 → 15.** Mega pin 16 is `Serial2` TX2, which this firmware owns
  for the Pi link (the reference used both — a latent conflict that never showed
  because its Serial2 debug output was disabled). Pin 15 (Serial3 RX3) is unused;
  Serial3 is never begun.
- **No wait-for-key at boot.** The reference blocked on LeftKey (pin 35). Here the
  watchdog + "no commands = motors idle" make auto-start safe; the blocking
  `calibrateGyro()` (~5 s, robot must stand still) is kept.
- **No strategy in `loop()`.** The reference sketch ran `followBallWithRotation()`;
  this firmware runs the executor loop only.
- Gyro offsets / motor pin pairs / line mux positions / kicker + dribbler + catcher
  pins are unchanged from `rcj_arduino_src/config.hpp` (per-robot `FORWARD` /
  `GOALKEEPER` select at the top of `src/config_fw.hpp`).
- Kicker pulse is non-blocking (40 ms scheduled) — the reference `delay(40)` would
  break the ≥100 Hz telemetry requirement.

## Wiring reminders (unchanged, for the bench)

- Pi GPIO14/15 (`/dev/serial0`) ↔ Mega TX2/RX2 (pins 16/17), common ground, 115200 8N1.
- The Pi's UART console must be disabled (`enable_uart=1`, `console=serial0,115200`
  removed from `cmdline.txt`) for `/dev/serial0` to be free.
- `robot_core` runs on the Pi with the camera core in `--no-serial` mode (the old
  `G<id>,…` camera lines would otherwise share the port).
