#pragma once
#include <Arduino.h>

// gyro_fw — MPU6050 DMP heading + drift-freeze, ported from
// rcj_arduino_src/gyro.cpp (READ-ONLY reference). This sensor logic STAYS on
// the Mega per the target split (PROMPT_ROBOT §3): the Mega owns the MPU6050
// at 100 Hz and streams heading + still-flag telemetry; the Pi consumes it.
//
// The blocking turn helpers of the reference (turnByAngle/turnToAngle/
// testTurnAngle/testAlignment) are NOT ported — they are strategy and move to
// robot_core as non-blocking states.

#include <MPU6050_6Axis_MotionApps20.h>

extern MPU6050 mpu;

// Runs the full gyro bring-up: mpu.initialize(), connection test,
// dmpInitialize() + the per-robot offsets from config_fw.hpp, DMP enable.
// Returns false when the sensor is absent/unresponsive (firmware keeps
// running, heading telemetry just never appears — HB still flows).
bool setupGyro();

// Blocking drift calibration for setup() (robot must stand still).
// Ported unchanged: settle wait (up to 12 s), median of 500 ms rate windows,
// bias sanity caps, post-settle wait. Default duration 1500 ms.
void calibrateGyro(unsigned int duration = 1500);

// Drains the DMP FIFO (all pending packets) and updates the compensated
// heading + drift-freeze state. Call every loop iteration. Returns true when
// at least one fresh DMP packet was consumed this call (the telemetry sender
// uses this to emit heading frames at the native DMP rate).
bool gyro_update();

// Current heading, wrapped -180..180 (the value the telemetry sends).
float gyro_heading();

// Raw (uncompensated, unfrozen) DMP yaw of the last packet — diagnostics.
float gyro_raw_yaw();

// Measured drift bias deg/s — diagnostics.
float gyro_bias();

// True while the heading is frozen (robot still) — the telemetry still flag.
bool gyro_is_still();
