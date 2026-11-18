# AGENTS.md

Arduino Mega 2560 firmware for an RCJ soccer robot. Flat Arduino IDE sketch: `RCJ_NEW.ino` plus one `.cpp/.hpp` per subsystem in the repo root. Not a git repo, no tests, no CI.

## Verify changes (compile only)

There is no `platformio.ini`, Makefile, README or `arduino-cli` in the repo. The verified check is PlatformIO (installed at `~/.platformio/penv/Scripts/platformio.exe`):

```powershell
$tmp = "$env:TEMP\rcj_build"; New-Item -ItemType Directory -Force "$tmp\src" | Out-Null
Copy-Item *.cpp,*.hpp,*.ino "$tmp\src\" -Force
@"
[env:megaatmega2560]
platform = atmelavr
board = megaatmega2560
framework = arduino
lib_deps =
    electroniccats/MPU6050
    arduino-libraries/Servo
"@ | Set-Content "$tmp\platformio.ini"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run --project-dir $tmp
```

- Success is the `[SUCCESS]` line plus the RAM/Flash report; exit code is 0. Ignore pre-existing warnings (`ball.cpp`, `controller.cpp`, `line.cpp`, `#pragma once` in `gyro.cpp`).
- The firmware cannot run on the host. Never claim a runtime or hardware test after only compiling.

## Entry point and modes

- `RCJ_NEW.ino` has `setup()` and `loop()`. `loop()` currently runs a straight-line test (`driveForward(3000)`); the four-direction demo and every other behaviour are commented out. Uncomment one call to test it, and comment it back afterwards.
- `config.hpp` selects the robot: `GOALKEEPER` is defined, `FORWARD` is not. Pin mapping (`Motors[]`), line thresholds (`line.hpp`) and MPU offsets (`hardware.cpp::setupGyro()`) are all per-mode.
- `setup()` blocks: ~5 s gyro calibration (the robot must stand still), then `while (!digitalRead(LeftKey));` — it waits until the left key (pin 35) is pressed.

## Motors

- `drive(leftFront, leftBack, rightFront, rightBack)` in `motors.cpp`. Order everywhere: LF, LB, RF, RB.
- Motor direction is fixed with `MotorInvert[4]` (`config.hpp`). Never swap `MotorXX1`/`MotorXX2` to flip a wheel: a half-swapped set inverts only some wheels and the robot then drives **diagonally** while the gyro still holds yaw (the gyro cannot see lateral motion). If it goes diagonally, run `rawTestWheels()` / `rawForward()`.
- Output path: `limitPWM()` scales all four proportionally so the max is `MotorPWM` (`config.hpp`), then `MotorMinPWM[4]` applies a per-wheel deadband floor. The floor is applied after the cap on purpose, so it can exceed `MotorPWM`.
- `MotorPWM` is a raw PWM cap (0..255), not a percent. It is currently 100 (~39% duty). Every command is clamped to it, so check it first when the robot feels powerless.
- `BS`/`LS`/`CS` (`motors.hpp`) are vector magnitudes, not wheel PWM: a straight move runs each wheel at ≈0.71·`BS`. `BS=100` → ≈70 PWM per wheel. `CS` is unused.
- `baseSpeed` is a mutable global and the default for `driveAngle()`/`driveAngleWithRotation()`; `alignBallGate()`, `catchBall()` and `driveFromLine()` switch it between `BS` and `LS` at runtime.
- Direction helpers in `controller.cpp`: `driveForward/Backward/Left/Right(duration, speed)` all call `driveDirection(moveAngle, ...)`. `moveAngle` 0 = forward, 180 = backward, +90 / -90 = sideways (swap these two if left/right are mirrored on the robot).
- `driveDirection()` is a PI heading controller. It captures the yaw at the start and speeds up/slows down the left vs right wheels to hold it; the integral cancels the constant pull from a weak/draggy wheel. The correction is clamped to `headingMaxShare` of the base wheel PWM so a wheel never reverses. The old `error*p + delta*d` loop was removed because `d=4.0` with a ±60 clamp made it bang-bang and it never went straight. `#define HEADING_DEBUG` in `controller.cpp` prints the live error/integral/correction. See «Drive / heading hold» below for the current law.
- Dribbler is a `Servo` on pin 12. On Mega Servo takes Timer5 (pins 44-46) for the first 12 servos; motor PWM pins are unaffected unless many more servos are attached (then Timer1/3/4 start to be used).
- Status LED: `StatusLed` (`config.hpp`) = spare pin 16; the on-board LED of the Mega is pin 13, which is also a motor PWM pin, so the status LED blinks on pin 16 instead. `setupStatusLed()` / `startStatusLedBlinking()` / `stopStatusLedBlinking()` / `updateStatusLed()` in `hardware.*`; blinking starts before `calibrateGyro()` and stops after it (i.e. during gyro init/calibration).

## Gyro (MPU6050 DMP)

- Yaw is pure gyro integration: there is no magnetometer, so no absolute heading. `calibrateGyro()` (default 2 s, 0.5 s median windows) runs in `setup()`.
- `getGyroAngle()` freezes the reported angle at standstill. It estimates the yaw rate over ≥100 ms: a change faster than `gyroMotionThreshold` (1 °/s, `gyro.cpp`) or a non-zero motor command counts as real motion and releases the freeze; a slower change is drift and is held. It freezes again after 300 ms without motion. Do **not** shorten that window to 50 ms — one 0.3° DMP jitter then reads as ~3 °/s and releases the freeze.
- The heading is frozen from the **very first** call (`haveFrozenOnce`), and a fast yaw is not treated as a turn until one calm rate sample has been seen (`startupSettled`). The DMP yaw sweeps ~170° in the first seconds after `dmpInitialize()`, and the operator may carry the robot while `setup()` waits for the key; without this hold `printGyro()` showed the angle running from 135 to -35 with the robot standing still. A motor command still counts as motion immediately.
- The rate must be estimated from `rawYaw`, never from the compensated angle: while frozen the compensation is re-based on every call, so the compensated angle is pinned and no turn could be detected.
- While frozen and still, `driftOffset` is re-based each call (`wrapAngle(rawYaw - frozenAngle)`). That makes the frozen value exact and stops a jump on release. It is **not** re-basing at unfreeze: on the call that detects motion the offset is left alone, so `compensated` = `frozenAngle` + the rotation since the last still sample. The older bug was re-basing exactly at unfreeze, which erased a turn made between two calls.
- `driveDirection()` sends one command before reading the heading, so it uses the real current angle rather than a frozen value.
- The `GyroAngle` operators `-` and `+` now return by value (they used to return a reference to the local operand, i.e. a dangling reference = UB).
- `calibrateGyro()` waits for the DMP yaw to stop moving (`waitYawSettled()`, up to 6 s, < 2.5 °/s) before measuring, because the DMP settles right after `dmpInitialize()` and that motion used to be measured as drift and rejected. If it never settles it prints `WARNING: gyro yaw is still moving` and only accepts a bias below 3 °/s. After measuring it waits for a calm yaw again (1.5 s) before returning.
- While idle, bias is re-learned from 0.5 s windows with `|rate| < 2 °/s` (`gyroBiasZ = 0.7*old + 0.3*rate`), so the freeze does not hide a stale bias.
- `printGyro()` prints `angle= raw= drift= still=`; `still=yes` means the heading is currently frozen.

## Drive / heading hold (gyro control loop)

- `driveDirection()` is a PI heading controller (gains in `controller.hpp`): `headingKp=2.0` (PWM per deg), `headingKi=1.5` (PWM per deg*s), `headingKd=0.4` (PWM per deg/s of yaw rate, damping), `headingInvert=1` (sign of the rotation), `headingMaxIntegral=30`, `headingMaxShare=0.5`. Correction = `headingInvert * (headingKp*error + headingKi*integral - headingKd*yawRate)`, clamped to ±`headingMaxShare` of the base wheel PWM.
- The integral is static (kept between drive calls) and is integrated only when the correction is not saturated. Re-learning it from zero in every ~1 s segment caused a square to rotate gradually.
- The yaw rate is sampled over ≥20 ms (the DMP yaw updates at 100 Hz); per-iteration rates are noise.
- `#define HEADING_DEBUG` in `controller.cpp` prints the live error/integral/rot every 200 ms while `driveDirection()` runs.
- `headingSignTest()` (debug.cpp) spins the robot in place (~1.5 s, command `drive(40,40,-40,-40)`) and tells whether positive rotation increases the yaw (`headingInvert=1`) or decreases it (`headingInvert=-1`). Run it on the floor before judging the controller.

## Approach to the ball (PD regulator)

- `drivePD(GyroAngle target, unsigned long duration, int speed, int moveAngle = 0)` is the ball-approach controller: drives the robot straight at `speed` and applies
  `torque = headingInvert * (headingKp * error - headingKd * yawRate)`, `error = target - current`, clamped to ±`headingMaxShare` of the base wheel PWM. Pure PD, **no integral** (approach is short).
  - `moveAngle` picks the motion axis (0 = straight forward, 180 = backward, ±90 = sideways), same convention as `driveAngle()`/`driveAngleWithRotation()`.
- Used by: `alignBallGate()` (to the ball while aligning), `catchBall()`, `score()`, `goalkeeper()`, `_goalkeeper_()`, `findGate()`, `driveFromLine()`.
- `turnToBall()` was rewritten to the same PD law (pure yaw torque toward a `target` heading, error ± damping, in-place `drive(rot,rot,-rot,-rot)`), replacing the fixed `drive(±45,±45,∓45,∓45)` turns.

## Sensors

- Nothing updates in the background: call `ball.updateStatus()`, `gate.updateStatus()`, `line.updateStatus()` before reading values.
- Ball: `Locator` over I2C at `0x0E` (angle/strength) plus a catcher switch on pin 14. There is no distance sensor: `distance = 10000 / strength`.
- Gate: data comes from the Raspberry Pi on `piSerial` (= `Serial2`, 115200) as `angle,area` lines; `HB` is a heartbeat. No data means stale values.
- Line: 16 sensors through a 4-channel mux on `A14`. `updateStatus()` uses `readSens_()` (per-sensor `lineSensorsTH[]`, averages two consecutive samples per sensor, single pass). The ADC runs at 500 kHz (prescaler set in `setupGPIO()`); `setMuxOut()` writes `PORTC`/`PORTD` directly (verified against `variants/mega/pins_arduino.h`: pin 38=PD7←bit0, pin 36=PC1←bit1, pin 34=PC3←bit2, pin 32=PC5←bit3) and reads go through `fastLineAdc()` (A14 = ADC14, `MUX5=1`, `ADMUX=0x46`, same AVcc reference as `analogRead`). `countAngle()` uses `static const` weight tables and does not print to Serial. The old (never-called) `readSens()`/median-3-of-samples and the `TH` global are kept as in the original but are unused paths, like before.

## Debug helpers (`debug.cpp`)

- `printGyro()`, `printBall()`, `printLine()` and `testKick()` block forever; `printLine()` also drives with a random angle. Enable at most one in `loop()`.
- `rawTestDrive()` spins each wheel alone at PWM 100 for 2 s — useful for finding a weak or stuck motor.
- `rawTestWheels()` spins LF, LB, RF, RB one at a time at PWM 80 (lift the robot) and prints the name — use it to find a reversed wheel. `rawForward()` drives all four at equal PWM with no gyro/mixing; on the floor it must go straight, so it separates a wiring/mapping fault from a controller fault.
- `Serial` is 115200. Sensor updates print unconditionally (`Tmp angle =`, `Final angle =`), so heavy debug output slows the control loop.
