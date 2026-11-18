#pragma once
// config_fw.hpp — pins, limits and tuning for the thin executor firmware.
//
// Values are ported 1:1 from UniKostyl/rcj_arduino_src/config.hpp (READ-ONLY
// reference) unless a comment says otherwise. The Mega is ONLY an executor:
// no strategy, no ball, no gate parsing, no line angle math live here.

// --- robot selection: exactly one of these is defined (same as config.hpp).
// The gyro offsets and nothing else depend on it.
#define FORWARD
// #define GOALKEEPER

// ---------------- motors ----------------
// Motors[i] pairs, order everywhere: LF, LB, RF, RB.
const int Motors[] = {3, 5, 7, 9, 11, 13, 8, 10};

// Raw PWM cap (0..255, not percent). Every wheel output is capped to this by
// limitPWM() AFTER per-wheel invert; MotorMinPWM[] is applied after the cap on
// purpose (the floor may exceed the cap). Ported from rcj motors.cpp path.
const int MotorPWM = 70;

// Deadband floor per wheel as soon as its command is non-zero. Order: LF, LB, RF, RB.
const int MotorMinPWM[4] = {0, 0, 0, 0};

// Per-wheel direction correction, order LF, LB, RF, RB (measured with
// rawTestWheels() on this robot: LF, LB, RB backwards, RF correct).
const int MotorInvert[4] = {1, 1, 0, 1};

#ifdef FORWARD
const int MotorLF1 = Motors[6];
const int MotorLF2 = Motors[7];
const int MotorLB1 = Motors[5];
const int MotorLB2 = Motors[4];
const int MotorRB1 = Motors[0];
const int MotorRB2 = Motors[1];
const int MotorRF1 = Motors[3];
const int MotorRF2 = Motors[2];
#endif

#ifdef GOALKEEPER
const int MotorLF1 = Motors[5];
const int MotorLF2 = Motors[4];
const int MotorLB1 = Motors[7];
const int MotorLB2 = Motors[6];
const int MotorRB1 = Motors[1];
const int MotorRB2 = Motors[0];
const int MotorRF1 = Motors[3];
const int MotorRF2 = Motors[2];
#endif

// ---------------- sensors ----------------
const int Mux[] = {38, 36, 34, 32};   // mux select bits 0..3 (PD7, PC1, PC3, PC5)
const int MuxOut = A14;               // mux analog out (ADC14)

// ---------------- executors ----------------
const int Dribbler_pin = 12;    // Servo
const int Kicker = 18;          // solenoid, active HIGH pulse
const int Catcher_pin = 14;     // microswitch, active LOW (rcj "Ball_detector")

// Status LED. CHANGED vs rcj (pin 16): Mega pin 16 is Serial2 TX2, which this
// firmware now owns for the Pi link. Pin 15 (Serial3 RX3) is unused here —
// Serial3 is never begun, so 15 works as a plain digital output.
const int StatusLed = 15;

// NOTE vs rcj config.hpp: LeftKey/RightKey/Enc*/setupScreen() are not ported —
// this firmware is a headless executor. Safety comes from the watchdog: with
// no Pi commands for WATCHDOG_MS the motors are stopped, so there is no need
// for the blocking wait-for-key gate at boot.

// ---------------- dribbler mapping (configurable, PROMPT_ROBOT §3) ----------------
// The rcj firmware left startDribbler()/stopDribbler() EMPTY stubs (the
// dribbler was never driven). The split adds the dribbler bit per the target
// split; the on/off servo positions are configurable here.
const int Dribbler_us_off = 1000;   // rcj setupDribbler() init position
const int Dribbler_us_on = 2000;    // "run" position (rcj commented 2000 test)

// ---------------- kicker ----------------
// Ported from rcj hardware.cpp kick(): 40 ms HIGH pulse, cooldown timer starts
// after the pulse ends (so start-to-start ≈ 1040 ms). Non-blocking here.
const unsigned long KickerPulseMs = 40;
const unsigned long KickerCooldownMs = 1000;

// ---------------- watchdog ----------------
// No valid command from the Pi for this long -> wheels 0, kicker off, dribbler
// off. Telemetry keeps streaming. See PROTOCOL.md §5.
const unsigned long WatchdogMs = 300;

// ---------------- link / telemetry rates ----------------
const unsigned long LinkBaud = 115200;
const unsigned long LinePollMs = 20;         // 50 Hz (PROTOCOL.md T_LINE)
const unsigned long CatcherLineMs = 100;     // 10 Hz sampling; bit rides in T_LINE
const unsigned long HeartbeatMs = 500;       // 2 Hz (T_HB)

// Set to 1 to mirror link frames as short text summaries on the USB debug
// console. The link itself is always binary (PROTOCOL.md §2 Option B).
#define FW_DEBUG_TEXT 0
