# robot_core — Pi-side strategy core (PROMPT_ROBOT phase 1, Idea 12)

C++17 program that holds ALL the game logic moved off the Arduino Mega: tactics
(ports of `rcj_arduino_src/controller.cpp`), ball Locator logic (`ball.cpp`),
line angle computation (`line.cpp`), omni mixing + the PI/PD heading laws
(`motors.cpp`/`controller.cpp` + `controller.hpp` gains), consuming the camera
gate status (PROMPT_EN §4, Idea 2 multipart JSON on ZMQ 5555).

The Mega counterpart (`UniKostyl/mega_fw/`) is a thin executor: wheel PWMs +
kicker/dribbler bits down, gyro/line/catcher telemetry up, 300 ms watchdog.

## Status

- **Pure logic + unit tests: verified on Windows** (see below).
- **IO layer (termios / i2c-dev / zmq) and the main binary: `// UNVERIFIED ON HW:`**
  — compiled only; nothing here has touched real hardware.
- The uart link uses the **APPROVED binary framing v1** (see
  `mega_fw/PROTOCOL.md`): fixed frames + CRC8, implemented in `src/link_codec.*`
  (Pi) and `mega_fw/src/link_frame.hpp` / `link_rx.cpp` / `link_tx.cpp` (Mega).
  The old text format is retired.

## Layout

```
src/
  angles.{hpp,cpp}        wrap helpers, Heading (GyroAngle port)
  omni.{hpp,cpp}          driveAngle / driveAngleWithRotation / spin (rcj motors.cpp)
  drive_limits.{hpp,cpp}  Pi-side mirror of the Mega safety path (cap/floor)
  heading_ctrl.{hpp,cpp}  PI (driveDirection) + PD (drivePD) laws, rate estimator
  ball_logic.{hpp,cpp}    Locator semantics (ball.cpp port), injectable I2C transport
  line_logic.{hpp,cpp}    binarize + countAngle with rcj weight tables (line.cpp port)
  gate_status.{hpp,cpp}   lenient Idea-2 status JSON parse + camera->robot angle
  link_codec.{hpp,cpp}    binary frame codec + telemetry parser (pure, golden-tested)
  tactics.{hpp,cpp}       ALL controller.cpp tactics as a non-blocking FSM
  config.{hpp,cpp}        every ported constant + split tunables
  main.cpp                control tick + locator + link + zmq (Linux only)  // UNVERIFIED ON HW:
  uart_link_linux.{hpp,cpp}   termios /dev/serial0  // UNVERIFIED ON HW:
  locator_i2c_linux.{hpp,cpp} i2c-dev 0x0E — VOLTAGE WARNING inside  // UNVERIFIED ON HW:
  zmq_status_sub.{hpp,cpp}    SUB tcp://127.0.0.1:5555 [JPEG, JSON]   // UNVERIFIED ON HW:
tests/
  sim.hpp                 simulator harness (robot plant, gyro, locator, lines, gates,
                          SimMega spec model: binary frame decode + watchdog + kicker cooldown)
  test_logic.cpp          the unit tests
vendor/
  nlohmann/json.hpp       v3.12.0 (copied from pi_core/vendor)
  zmq.hpp                 cppzmq v4.11.0 (copied from pi_core/vendor; Pi binary only)
```

## Build & verify on Windows (PC-first, PROMPT_ROBOT §7)

Only the pure logic + tests compile on Windows (no OpenCV/ZMQ/termios; the
nlohmann header is vendored). Same tool previous agents used for pi_core:

```bash
mkdir -p UniKostyl/robot_core/build
python -m ziglang c++ -O2 -std=c++17 \
  -I UniKostyl/robot_core/src -I UniKostyl/robot_core/vendor -I UniKostyl/robot_core/tests \
  UniKostyl/robot_core/tests/test_logic.cpp \
  UniKostyl/robot_core/src/angles.cpp UniKostyl/robot_core/src/omni.cpp \
  UniKostyl/robot_core/src/drive_limits.cpp UniKostyl/robot_core/src/heading_ctrl.cpp \
  UniKostyl/robot_core/src/ball_logic.cpp UniKostyl/robot_core/src/line_logic.cpp \
  UniKostyl/robot_core/src/gate_status.cpp UniKostyl/robot_core/src/link_codec.cpp \
  UniKostyl/robot_core/src/tactics.cpp UniKostyl/robot_core/src/config.cpp \
  -o UniKostyl/robot_core/build/robot_core_tests.exe
./UniKostyl/robot_core/build/robot_core_tests.exe
```

Green = the final `N checks, 0 failures` line and exit code 0.

## Build on the Pi (deploy target)

```bash
sudo apt install -y build-essential cmake libzmq3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 3
./build/robot_core_tests        # logic tests run anywhere
```

`robot_core` (the binary) builds only on Linux (termios/i2c-dev/zmq).

## Run (on the Pi; UNVERIFIED ON HW)

```bash
./build/robot_core --role forward --tick-hz 100            # camera on 127.0.0.1:5555
./build/robot_core --no-gates --no-serial                  # bench: print TX frame summaries, no camera
./build/robot_core --role goalkeeper --gk-variant classic  # classic goalkeeper()
```

The camera core must run with `--no-serial` while robot_core owns `/dev/serial0`
(the old `G<id>,…` camera lines and the new drive/telemetry link share the port;
documented in PROMPT_ROBOT §4). Flags: `--role`, `--gk-variant`, `--demo`,
`--tick-hz`, `--serial-dev`, `--no-serial`, `--i2c-dev`, `--no-i2c`,
`--gates-endpoint`, `--no-gates`, `--duration`, `--verbose`.

**Locator voltage (PROMPT_ROBOT §3):** the Mega is 5 V-logic; the Pi I2C is
3.3 V. `locator_i2c_linux.hpp` carries the full warning — the module's supply
and pull-ups must be measured before connecting; a 5 V device needs a level
shifter. NOT yet measured (hardware step for the user); nothing assumes a value.

## FSM: states and their rcj sources

| FSM state | rcj source | Exit condition |
|---|---|---|
| `IDLE` | standby (`drive(0,0,0,0)` legs) | entry rules per role/mode |
| `TURN_TO_BASE` | `play()` + `turnToAngle(baseAngle,45,3,10)` | `|delta| <= 3` (10x time cap added) |
| `ALIGN_BALL_GATE` | `alignBallGate()` | `|ball.angle| < 5` |
| `CATCH_BALL` | `catchBall()` | catcher bit or `|angle| >= 10` |
| `SCORE` | `score()` | gate area >= 2000 or ball released → kick |
| `FIND_GATE` | `findGate()` | area >= 3000 (or 0/absent) |
| `TURN_TO_BALL` | `turnToBall()` | `|error| < 5` (3 s cap) |
| `GK_DEFEND_SHORT` | `_goalkeeper_()` defend window | 500 ms or ball far → kick |
| `GK_CLASSIC_DEFEND` | `goalkeeper()` defend window | 1500 ms |
| `GK_CLASSIC_HOME` | `goalkeeper()` "going home" | `\|gate.angle\| <= 10` or area >= 1500 |
| `LINE_ESCAPE_DRIVE` | `checkLine()` + `driveFromLine()` phase 1 | 30 s cap (configurable) |
| `LINE_ESCAPE_WAIT` | `driveFromLine()` phase 2 | `line.angle == -1` |
| `FOLLOW_BALL` | `followBallWithRotation()` (debug.cpp) | ball-only fallback / demo |

`checkLine()` preemption: the line escape preempts every other state (as the
rcj call inside every blocking loop did) and returns to the interrupted state.

## Documented deviations from the reference (all "no bugs" driven, PROMPT_ROBOT §7)

1. **Frame composition switch** (`bearing_targets_relative`, default **false** =
   rcj): rcj `drivePD(GyroAngle(ball.angle + 60))` treats a RELATIVE bearing as
   an ABSOLUTE DMP heading — correct only while the heading stays near zero
   (play() re-heads to baseAngle to enforce that). The faithful default is kept;
   `true` composes bearing + current heading (what `turnToBall()` already did).
   The closed-loop sim tests run with `true` (physical convergence); the default
   semantics are asserted separately.
2. **Gates absent → degrade, never blind-drive**: with no fresh camera status,
   `SCORE` falls back to `FOLLOW_BALL` (reason logged, no blind kick) and
   classic-goalkeeper "home" goes to standby — rcj drove on stale gate angles.
3. **Blind-drive/turn guards**: `CATCH` stops after 800 ms of locator-not-found
   (rcj drove on the stale bearing); `TURN_TO_BALL` is skipped when the ball is
   absent (rcj spun toward garbage); `FOLLOW_BALL` stops when the ball is lost.
4. **Telemetry stale → zero commands** (Pi side) on top of the Mega's 300 ms
   watchdog — two independent kill paths.
5. **Time caps added** where rcj could spin forever (re-heading 10x nominal).
6. **Kicker pulse is non-blocking** on the Mega (rcj `delay(40)` inside kick()).
7. **asin domain clamp** in the tangent computation (rcj produced NaN for
   distance < 300; the port saturates at 90 deg, identical on the valid domain).
8. **Dribbler bit**: rcj never drove the dribbler (empty stubs); the split adds
   the bit per PROMPT_ROBOT §3 — ON during ball-hold states (configurable).

## CPU budget

The tick work per 10 ms is integer math + one JSON parse per camera frame
(25.9 fps) — microseconds; the locator thread does 4 single-byte I2C reads
every 20 ms. No busy-wait anywhere (`sleep_until` fixed rates). The per-second
`[tick]` log line prints the measured CPU share of one core (PROMPT_ROBOT §7).
Measured on PC: see the report; Pi measurement pending hardware.
