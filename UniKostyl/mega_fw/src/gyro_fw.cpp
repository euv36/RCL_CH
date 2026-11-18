#include <Arduino.h>
#include "gyro_fw.hpp"
#include "motors_fw.hpp"
#include "config_fw.hpp"

// Ported from rcj_arduino_src/gyro.cpp + hardware.cpp::setupGyro() (READ-ONLY
// reference). Comments below are kept where they encode hardware-verified
// reasons; new code (setupGyro offsets pulled from config_fw.hpp) is marked.

MPU6050 mpu;

uint8_t fifoBuffer[64];
Quaternion q;         // [w, x, y, z]         quaternion container
VectorFloat gravity;  // [x, y, z]            gravity vector
float ypr[3];         // [yaw, pitch, roll]   yaw/pitch/roll container

// Drift compensation
// The DMP yaw is a pure gyro integration, so a small gyro bias makes the
// heading slowly crawl. We measure that bias while the robot stands still
// and subtract it from the reported angle afterwards.
float gyroBiasZ = 0.0f;              // deg/s, measured DMP yaw drift
static float driftOffset = 0.0f;     // deg, accumulated bias compensation
static float rawYaw = 0.0f;          // last yaw straight from the DMP
static bool gyroStill = false;       // true while the heading is frozen
static unsigned long lastGyroMicros = 0;
static bool gyroInitialized = false;
static bool dmpReady = false;        // NEW: sensor present flag
static float reportedHeading = 0.0f; // last frozen/compensated heading, deg

// Yaw rates below this (deg/s) are treated as drift, not as a real turn.
// A hand turn or a motor turn is faster; slow settling/drift is not.
static const float gyroMotionThreshold = 1.0f;

static float wrapAngle(float angle) {
  while (angle > 180.0f) angle -= 360.0f;
  while (angle < -180.0f) angle += 360.0f;
  return angle;
}

bool setupGyro() {
#ifdef FORWARD
  const int16_t accel_offset[3] = { -2961, 439, 1193 };
  const int16_t gyro_offset[3] = { 77, 90, -30 };
#endif
#ifdef GOALKEEPER
  const int16_t accel_offset[3] = { -3531, -1223, 1339 };
  const int16_t gyro_offset[3] = { -1872, 13, -35 };
#endif
  Serial.println("Gyro initializing...");
  mpu.initialize();
  Serial.println("Testing connection...");
  if (!mpu.testConnection()) {
    Serial.println(F("MPU6050 connection failed"));
    dmpReady = false;
    return false;
  }
  Serial.println(F("MPU6050 connection - OK"));
  mpu.dmpInitialize();
  mpu.setXGyroOffset(gyro_offset[0]);
  mpu.setYGyroOffset(gyro_offset[1]);
  mpu.setZGyroOffset(gyro_offset[2]);
  mpu.setXAccelOffset(accel_offset[0]);
  mpu.setYAccelOffset(accel_offset[1]);
  mpu.setZAccelOffset(accel_offset[2]);
  mpu.setDMPEnabled(true);
  dmpReady = true;
  return true;
}

// Waits until the DMP yaw stops moving (below maxRate deg/s over a 250 ms
// window), draining the FIFO as it goes. Returns true when it settled.
// The DMP yaw can sweep tens of degrees per second for several seconds right
// after dmpInitialize(). Without this wait that sweep is measured as "drift",
// gets rejected as too noisy, the real bias stays uncompensated, and the first
// reported angle runs away while the robot is standing still.
static bool waitYawSettled(unsigned long timeoutMs, float maxRate) {
  float refYaw = 0.0f;
  unsigned long refTime = millis();
  unsigned long start = millis();
  bool haveRef = false;
  while (millis() - start < timeoutMs) {
    if (!mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
      continue;
    }
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    float yaw = degrees(ypr[0]);
    rawYaw = yaw;
    unsigned long now = millis();
    if (!haveRef) {
      refYaw = yaw;
      refTime = now;
      haveRef = true;
      continue;
    }
    if (now - refTime < 250) {
      continue;
    }
    float rate = wrapAngle(yaw - refYaw) / ((now - refTime) / 1000.0f);
    if (abs(rate) < maxRate) {
      return true;
    }
    refYaw = yaw;
    refTime = now;
  }
  return false;
}

// Measures the yaw drift while the robot is standing still and stores it in
// gyroBiasZ. Call it in setup() with the robot on the floor, motors stopped.
void calibrateGyro(unsigned int duration) {
  if (!dmpReady) {
    Serial.println("Gyro calibration skipped: no MPU6050");
    return;
  }
  Serial.println("Gyro calibration: keep the robot still (settling + measuring)...");
  unsigned long waitStart = millis();
  bool packetReady = false;
  while (!packetReady && millis() - waitStart < 2000) {
    if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
      packetReady = true;
    }
  }
  if (!packetReady) {
    Serial.println("Gyro calibration failed: no DMP packet");
    return;
  }

  // Drop the packets the DMP produced while it was starting up.
  while (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
  }

  // The DMP yaw sweep takes longer than 6 s on many boards; wait up to 12 s
  // before measuring the bias, or the calibration will never find it and the
  // heading will slowly run away right after boot.
  Serial.println("Waiting for the gyro yaw to settle...");
  bool settled = waitYawSettled(12000, 2.0f);
  if (!settled) {
    Serial.println("WARNING: gyro yaw is still moving, heading may run at start");
  }
  while (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
  }

  // The measurement is split into short windows and the median is used, so a
  // single accidental nudge does not spoil the whole calibration.
  const int MaxWindows = 20;
  const unsigned long WindowMs = 500;
  float windowRate[MaxWindows];
  int windowTarget = duration / WindowMs;
  if (windowTarget < 3) windowTarget = 3;
  if (windowTarget > MaxWindows) windowTarget = MaxWindows;
  int windowIndex = 0;
  float windowStartYaw = 0.0f;
  unsigned long windowStart = millis();
  float lastYaw = 0.0f;
  bool first = true;
  unsigned long startTime = millis();

  while (windowIndex < windowTarget && millis() - startTime < duration + WindowMs) {
    if (!mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
      continue;
    }
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    float yaw = degrees(ypr[0]);
    unsigned long now = millis();
    lastYaw = yaw;
    if (first) {
      windowStartYaw = yaw;
      windowStart = now;
      first = false;
      continue;
    }
    if (now - windowStart >= WindowMs) {
      float delta = yaw - windowStartYaw;
      if (delta > 180.0f) delta -= 360.0f;
      else if (delta < -180.0f) delta += 360.0f;
      windowRate[windowIndex++] = delta / ((now - windowStart) / 1000.0f);
      windowStartYaw = yaw;
      windowStart = now;
    }
  }

  // Median of the measured windows
  for (int i = 1; i < windowIndex; i++) {
    float key = windowRate[i];
    int j = i - 1;
    while (j >= 0 && windowRate[j] > key) {
      windowRate[j + 1] = windowRate[j];
      j--;
    }
    windowRate[j + 1] = key;
  }
  float bias = (windowIndex > 0) ? windowRate[windowIndex / 2] : 0.0f;
  // If the yaw never settled, a large "bias" is really leftover startup
  // motion: subtracting it would over-correct the heading.
  float maxBias = settled ? 10.0f : 3.0f;
  if (windowIndex > 0 && abs(bias) < maxBias) {
    gyroBiasZ = bias;
  } else {
    Serial.println("Gyro calibration looks noisy, keeping the old bias");
  }
  // Do not hand over to the main loop until the yaw is calm again.
  rawYaw = lastYaw;
  waitYawSettled(1500, 1.5f);
  while (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
  }
  // Restart the compensation from the current yaw
  driftOffset = 0.0f;
  lastGyroMicros = micros();
  gyroInitialized = true;

  Serial.print("Gyro drift = ");
  Serial.print(gyroBiasZ, 3);
  Serial.print(" deg/s (");
  Serial.print(windowIndex);
  Serial.println(" windows)");
}

// The drift-freeze heading update — ported line-for-line from
// getGyroAngle() (rcj gyro.cpp), minus the GyroAngle wrapper and the strategy
// helpers. Returns true when a fresh DMP packet was consumed.
bool gyro_update() {
  static bool havePacket = false;
  static bool frozen = false;       // heading is held because the robot is still
  static float frozenAngle = 0.0f;
  static bool haveFrozenOnce = false;        // hold the heading from the first call
  static bool stillTracking = false;
  static unsigned long stillRefMicros = 0;
  static float lastRateYaw = 0.0f;           // for the yaw rate estimate
  static unsigned long lastRateMicros = 0;
  static float yawRate = 0.0f;               // deg/s, low-passed
  static bool haveRate = false;              // at least one rate sample taken
  static bool startupSettled = false;        // DMP startup sweep is over
  static bool measuringDrift = false;
  static float driftStartYaw = 0.0f;
  static unsigned long driftStartMicros = 0;

  bool fresh = false;
  float compensated = 0.0f;

  // Use every packet that is already in the FIFO, so the angle is never stale
  // even if the loop was busy (link, servo, kicking...).
  while (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    rawYaw = degrees(ypr[0]);
    havePacket = true;
    fresh = true;
  }

  unsigned long now = micros();
  if (!gyroInitialized) {
    lastGyroMicros = now;
    gyroInitialized = true;
  }
  float dt = (now - lastGyroMicros) / 1000000.0f;
  lastGyroMicros = now;
  if (dt < 0.0f || dt > 5.0f) {
    dt = 0.0f;  // startup or an absurd gap: do not compensate it
  }
  driftOffset += gyroBiasZ * dt;

  compensated = wrapAngle(rawYaw - driftOffset);

  if (havePacket && dmpReady) {
    bool idle = motorsIdle();

    // Estimate the yaw rate from the RAW DMP yaw, not from the compensated
    // one: while the heading is frozen the compensation is re-based on every
    // call, so the compensated angle never moves and no turn could ever be
    // detected. Use a window of at least 100 ms: a single 0.3 deg jitter of
    // the DMP then looks like ~3 deg/s and would release the freeze on its own.
    if (lastRateMicros == 0) {
      lastRateYaw = rawYaw;
      lastRateMicros = now;
    }
    float rateDt = (now - lastRateMicros) / 1000000.0f;
    if (rateDt >= 0.1f) {
      float rate = wrapAngle(rawYaw - lastRateYaw) / rateDt;
      yawRate = 0.5f * yawRate + 0.5f * rate;
      lastRateYaw = rawYaw;
      lastRateMicros = now;
      haveRate = true;
    }

    // Moving = the motors are commanded, or the robot is really turning.
    // A slow change (< gyroMotionThreshold) is drift and is ignored, so the
    // heading stays put. Until the rate has actually been measured once and is
    // calm, do not treat a fast yaw as a turn: right after dmpInitialize() the
    // DMP yaw itself sweeps, and that sweep is not motion of the robot in play.
    // A motor command still counts as motion, so driving at boot is not blocked.
    if (!startupSettled && haveRate && (!idle || abs(yawRate) <= gyroMotionThreshold)) {
      startupSettled = true;
    }
    bool moving = !idle || (startupSettled && abs(yawRate) > gyroMotionThreshold);

    if (!haveFrozenOnce) {
      // First call after calibration: hold the heading immediately. The DMP
      // yaw sweeps for several seconds after dmpInitialize(), and the operator
      // may carry the robot while setup runs; none of that is a turn of the
      // robot in play.
      haveFrozenOnce = true;
      frozen = true;
      frozenAngle = compensated;
      driftOffset = wrapAngle(rawYaw - frozenAngle);
      stillTracking = false;
    } else if (moving) {
      frozen = false;
      stillTracking = false;
    } else if (frozen) {
      // Absorb the drift into the compensation, so the frozen heading is exact
      // and releasing it later cannot jump by the accumulated drift. The
      // re-base only runs while the robot is still, so a real turn is never
      // erased: on the call that detects the turn the offset is left untouched
      // and compensated already contains frozenAngle + that rotation.
      driftOffset = wrapAngle(rawYaw - frozenAngle);
    } else {
      // Freeze only after the heading has been still for 300 ms.
      if (!stillTracking) {
        stillTracking = true;
        stillRefMicros = now;
      } else if (now - stillRefMicros > 300000UL) {
        frozen = true;
        frozenAngle = compensated;
        driftOffset = wrapAngle(rawYaw - frozenAngle);
        stillTracking = false;
      }
    }

    // Learn the real gyro bias while the robot is idle: a full 1 s window with
    // a slow yaw rate is pure drift, so blend it into gyroBiasZ.
    if (idle) {
      if (!measuringDrift) {
        measuringDrift = true;
        driftStartYaw = rawYaw;
        driftStartMicros = now;
      } else if (now - driftStartMicros > 1000000UL) {
        float measured = wrapAngle(rawYaw - driftStartYaw);
        float rate = measured / ((now - driftStartMicros) / 1000000.0f);
        if (abs(rate) < 2.0f) {
          gyroBiasZ = 0.7f * gyroBiasZ + 0.3f * rate;
        }
        driftStartYaw = rawYaw;
        driftStartMicros = now;
      }
    }
  }

  gyroStill = frozen;
  reportedHeading = frozen ? frozenAngle : compensated;
  return fresh;
}

float gyro_heading() {
  // Same value the reference getGyroAngle() reported: frozenAngle while the
  // heading is frozen, else the drift-compensated raw yaw; wrapped -180..180.
  return reportedHeading;
}

float gyro_raw_yaw() {
  return rawYaw;
}

float gyro_bias() {
  return gyroBiasZ;
}

bool gyro_is_still() {
  return gyroStill;
}
