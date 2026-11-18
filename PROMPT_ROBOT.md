# PROMPT_ROBOT.md — отдельный промпт: перенос игровой логики RCJ_NEW с Arduino Mega на Raspberry Pi

> **Для пользователя (RU):** камера сейчас в приоритете — этот промпт для ОТДЕЛЬНОГО агента, который не трогает камерные файлы. Запускать, вставив файл целиком. Эталонный код уже в репо: `UniKostyl/rcj_arduino_src/`. Агент обязан согласовать с вами формат uart-протокола до реализации.

---

You are working in the repository `C:\Users\sweetener\Desktop\camera` (Windows laptop, Git Bash shell). Task: split an existing Arduino Mega 2560 soccer-robot firmware so that **all strategy runs on the Raspberry Pi in C++**, and the Arduino becomes a thin motion/gyro/kicker executor.

**Priority rule:** the camera workstream is currently the priority. You must NOT touch camera-side files and must not disturb camera agents: do not edit `cv_kostyl.py`, `unikostyl.py`, `rpi_elp_cam.py`, `UniKostyl/pi_core/`, `fisheye*.py`, `widgets.py`, `out/`, `PROMPT*.md`, `PLAN.md`, `camera.bat`. Work only in your own files (below). Register in `COORDINATION.md` (create it if missing): agent/task, files you WRITE, files you READ, status `in_progress` → `done`. Never `git commit/push/checkout/reset`.

## 1. Read this first

1. `UniKostyl/rcj_arduino_src/AGENTS.md` — hardware-verified facts about the firmware you are porting (including the PlatformIO build recipe).
2. All sources in `UniKostyl/rcj_arduino_src/` — the reference firmware; treat it as READ-ONLY.
3. Root `AGENTS.md` and `UniKostyl/AGENTS.md` — repo/deploy facts (Pi address discovery via `arp -a`, MAC `d8:3a:dd`, pscp/plink deploy with the user's password, user `twox`, smoke tests).
4. `PROMPT_EN.md` section 4 (contracts) and section 5 Ideas 10–12 — the camera project's contracts you must not break and the contract you will CONSUME (camera gate status JSON).

## 2. Current state (facts from `rcj_arduino_src`)

The Mega firmware currently holds everything — that is the problem (the MCU "can't keep up", per the user): omni motor mixing + PI/PD heading hold (`motors.cpp`, `controller.cpp`, gains in `controller.hpp`), MPU6050 DMP gyro with drift-freeze logic (`gyro.cpp`, DMP at 100 Hz — timing-critical), IR ball Locator over I2C **0x0E** (`ball.cpp`: registers 0x04 angle, 0x05 strength, 0x06 angleClose, 0x07 strengthClose; `distance = 10000/strength`; tangent-angle logic), 16 line sensors through a 4-channel mux on A14 (`line.cpp`: thresholds, weight tables, angle computation), gate data received from the Pi over Serial2 115200 as legacy `angle,area` lines (`gate.cpp`), kicker pin 18, dribbler Servo pin 12, catcher microswitch pin 14, roles `GOALKEEPER`/`FORWARD` via `config.hpp`, `MotorPWM=70` cap, `MotorInvert[4]`, `MotorMinPWM[4]`, `BS=100`/`LS=60`, status LED pin 16.

## 3. Target split (the user's decision — do NOT redesign it)

- **Arduino — new thin firmware, `UniKostyl/mega_fw/`:** receive **4 wheel PWM values + a kicker bit + a dribbler bit** from the Pi; apply `MotorInvert[]`/limits/`MotorMinPWM[]` and drive (the existing motors.cpp safety path); the MPU6050 DMP + drift-freeze logic STAYS on the Mega (it owns the sensor at 100 Hz) and streams heading telemetry (with a `still` flag) at **≥100 Hz**; polls the 16 line sensors via mux+ADC and streams **RAW values** (no angle computation on the Mega); streams the catcher switch state; executes the kicker/dribbler bits (dribbler bit → servo on/off position, mapping configurable); **watchdog: no commands from the Pi for >300 ms (configurable) → all motors stop**. Nothing else: no strategy, no ball, no gate parsing, no line angle math.
- **Raspberry Pi — new C++ program, `UniKostyl/robot_core/`:** the Locator connects to the **Pi's I2C bus** (SDA1/SCL1, GPIO2/3) — read registers 0x04..0x07 and port `ball.cpp` logic (angle/tangent/distance); port `line.cpp` — 16 raw values arrive over uart, compute the line angle on the Pi with the existing weight tables; port ALL tactics from `controller.cpp` (`play()`, goalkeeper/forward roles, `alignBallGate`, `catchBall`, `score`, `findGate`, `driveFromLine`, `followBallWithRotation`, `turnToBall`) as a **non-blocking state machine** at a fixed 50–100 Hz loop in a dedicated thread; omni mixing (angle/speed/rotation → 4 wheel PWM) and the PI/PD heading laws (`driveDirection`/`drivePD`, gains from `controller.hpp`) run ON THE PI, closed on the gyro telemetry; kicker/dribbler decision bits; gate data comes from the camera side (section 4).
- **VOLTAGE WARNING:** the Mega is 5V-logic; the Pi's I2C is 3.3V. Check the Locator module's supply/pull-ups; if it is a 5V I2C device — a level shifter is mandatory before connecting to the Pi. State the measured/assumed voltage in your report; do not silently assume.

## 4. Gate data and camera interplay

- Gate angles for the tactics come from the camera pipeline. The camera project (`PROMPT_EN.md`) publishes on ZMQ 5555 a multipart `[JPEG, JSON status]` where the JSON has `{"gates":{"1":{"found":..,"angle":..,"area":..},...},"fps":..}` (Idea 2). `robot_core` SUBSCRIBES to that and consumes it.
- You do NOT implement the camera side. Until the camera status exists, run with a stub input (recorded JSON / null gates) — degrade gracefully to ball-only/line-only modes, log the reason, never block, never crash.
- The old pair (`uart_rpi.ino` + `rpi_elp_cam.py`) keeps working as rollback; your new pair (`mega_fw` + `robot_core`) replaces it only after acceptance. Delete nothing.
- **Port ownership:** the camera core writes the old `G<id>,…` lines to `/dev/serial0`. When running your new pair, the camera side must run with `--no-serial` (the flag exists in `rpi_elp_cam.py` and is mirrored by the future C++ core). Document this in your report.

## 5. Required tactic: «Новичок» ("Novice") — the user's reference attack pattern

The robot must execute this exact 5-step attack (from the user's diagram; yellow gate = ours, blue = opponent, as in the picture):

1. **Ball deep in our own half** (trigger): the ball is in our half near the own gate — switch into this tactic from the default play. Trigger threshold configurable.
2. **Face the side wall, move close to it:** choose the nearer side wall, drive to it and move close while keeping the ball under the dribbler (dribbler bit ON for the whole route); stop at a configurable safe gap from the wall.
3. **Run along the wall, across the field:** drive parallel to that wall toward the opponent's end, ball held; heading hold parallel to the wall via the gyro telemetry (the ported PI/PD laws).
4. **Corner cut to the end line:** at the opponent's end, cut diagonally to the end line (in front of the opponent gate) — the "corner cut" maneuver.
5. **Turn to the far post and shoot:** turn to the **far post** of the opponent goal, align within a configurable aim tolerance, fire the kicker (respecting its charge/cooldown), then return to the default play behavior.

Hard requirements:
- **Never leave the field** (penalty per regulation — see `PROMPT_EN.md` facts): every leg keeps a configurable boundary margin; the boundary fusion (phototransistor board + camera; camera project Idea 11) can abort or adjust the route at any step.
- **Orientation:** own vs opponent gate via the camera gate status + gate roles (camera project Idea 9); field geometry via the field binding (`field_map.json`, camera project Idea 10) when available. If gates are not published/visible, the tactic must not start blind: log the reason and fall back to the default ball-follow tactics.
- **Ball loss mid-route** (catcher opens or the Locator loses the ball for T ms) → abort to the default ball-follow; never keep dribbling "nothing".
- Implement as an explicit FSM (e.g., `NOVICE_TRIGGER → NOVICE_TO_WALL → NOVICE_ALONG_WALL → NOVICE_CORNER_CUT → NOVICE_AIM → SHOOT → default`), every transition logged, all parameters (trigger threshold, wall gap, boundary margins, aim tolerance, kicker cooldown) in a config file.
- Unit-testable on PC (section 8): synthetic ball/gyro/gate/line streams must drive the FSM through all 5 states, plus the abort paths (ball lost, boundary danger, gates missing).

## 6. UART protocol — design first, get user approval, then implement

- Bidirectional over the same Mega Serial2 link (Pi `/dev/serial0`, 115200).
- Pi→Arduino: a frame carrying 4 signed wheel PWMs (−255..255, before invert/limit), the kicker bit, the dribbler bit, plus an explicit stop command. Arduino→Pi: heading in degrees (with the still flag), 16 raw line values, the catcher bit, a heartbeat.
- Propose a robust framing (newline-delimited text or fixed binary + checksum — pick one and justify: lost-byte safety, parsing cost at 100 Hz). No busy-wait on either side; the Arduino loop must stay >100 Hz with line polling included (keep the ADC-prescaler trick from `hardware.cpp`).
- Write the spec to `UniKostyl/mega_fw/PROTOCOL.md` and **get the user's approval of the format BEFORE implementing both sides**. Watchdog timeout configurable.

## 7. Global requirements

- Maximum code in C++ on both sides. Pi: C++17, CMake, `-O3`, fixed-rate loops, no busy-wait, no unhandled exceptions, no per-frame allocation growth. The Pi also runs the camera pipeline — CPU budget: `robot_core` ≤ ~1 core, document measured CPU. Do not eat the camera's resources (no spinning threads).
- **No bugs — the robot must not go insane:** watchdog on the Arduino, checksummed framing, degraded modes (ball-only without gates), clamp every computed PWM, angle wrap 359→1 handled, I2C read failures (Locator absent/busy) → stale-value policy with timeout, never block the control loop on I2C or uart.
- PC-first: `robot_core` logic must build and be unit-testable on Windows (pure logic separated from IO; a simulation harness for ball/lines/gyro/gates so tactics can be tested without hardware). The Mega firmware is verified by PlatformIO compile only — **never claim runtime/hardware tests that did not happen**.
- Deploy to the Pi via pscp/plink (ask the user for the password).

## 8. Verification

- PlatformIO build of `mega_fw` (`[SUCCESS]` + RAM/Flash report; pre-existing warnings in ball/controller/line are known — see `rcj_arduino_src/AGENTS.md`).
- PC unit tests: omni mixing (driveAngle → wheel PWMs incl. the rotation term), heading PI on a synthetic gyro stream, line angle from synthetic raw arrays, locator distance curve (`10000/strength`), tactics FSM transitions with simulated sensors.
- On-Pi smoke (when hardware is available; ask for the password): `robot_core` runs with stub gates; `i2cdetect` finds 0x0E; telemetry flows from the Mega; motors respond to manual low-speed PWM commands with the robot lifted; killing `robot_core` stops the motors via the watchdog.
- Honest report: verified vs not verified on hardware.

## 9. Completion report

- Files created/changed; the protocol spec (`mega_fw/PROTOCOL.md`) and what the user approved; verified vs not verified; `COORDINATION.md` updated; how `robot_core` consumes camera status and what happens without it; new dependencies (apt/pip), if any.
