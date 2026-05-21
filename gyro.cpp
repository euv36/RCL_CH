#include <Arduino.h>
#include "config.hpp"
#include "gyro.hpp"
#include "drive.hpp"
#include <MPU6050_6Axis_MotionApps20.h>
#include "UI.hpp"

MPU6050 mpu;
GyroAngle gyroAngle;


uint8_t fifoBuffer[64];
Quaternion q;         // [w, x, y, z]         quaternion container
VectorFloat gravity;  // [x, y, z]            gravity vector
float ypr[3];         // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

bool initGyro() {
  // const int16_t accel_offset[3] = { -1764, 319, 803 };
  // const int16_t gyro_offset[3] = { 110, 6, 34 };
  const int16_t accel_offset[3] = { -2912, -1099, 1191 };
  const int16_t gyro_offset[3] = { -57, -17, 41 };
  // Wire.begin();
  // Wire.setClock(1000000UL);
  Serial.println("Gyro initializing...");
  mpu.initialize();
  Serial.println("Testing connection...");
  if (!mpu.testConnection()) {
    Serial.println(F("MPU6050 connection failed"));
    return false;
  } else {
    Serial.println(F("MPU6050 connection - OK"));
  }

  mpu.dmpInitialize();

  mpu.setXGyroOffset(gyro_offset[0]);
  mpu.setYGyroOffset(gyro_offset[1]);
  mpu.setZGyroOffset(gyro_offset[2]);
  mpu.setXAccelOffset(accel_offset[0]);
  mpu.setYAccelOffset(accel_offset[1]);
  mpu.setZAccelOffset(accel_offset[2]);

  mpu.setDMPEnabled(true);
  // PCICR |= (1 << PCIE2);
  // PCMSK2 |= (1 << PCINT22);
  return true;
}

GyroAngle& getGyroAngle() {
  if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
    // calculate angles
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    // mpuReady = false;
    // result (-3.14, 3.14) (-180.00  : 180.00)
    gyroAngle = degrees(ypr[0]);
  }
  return gyroAngle;
}

void printGyro() {
  GyroAngle angle;
  while (1) {
    angle = getGyroAngle();
    Serial.println(angle.rawValue());
    delay(500);
    // clear(Buffer);
  }
}

void turnByAngle(int angle, int speed, float correction, float gap) {
  drive(0, 0, 0, 0);
  GyroAngle startAngle = getGyroAngle();
  GyroAngle finishAngle = startAngle + angle;
  turnToAngle(finishAngle, speed, gap, correction);
  drive(0, 0, 0, 0);
}

void turnToAngle(GyroAngle goalAngle, int speed, float gap, float correction) {
  drive(0, 0, 0, 0);
  GyroAngle currentAngle = getGyroAngle();
  static GyroAngle finishAngle = currentAngle;
  GyroAngle startAngle = currentAngle;
#ifdef debug
  BT.print("Start/Finish delta = ");
  BT.println(abs(finishAngle.value() - startAngle.value()));
  BT.print("Start angle = ");
  BT.println(startAngle.value());
#endif
  GyroAngle delta = goalAngle - currentAngle;
  byte direction = CW;
  if (delta.rawValue() < 0) {
    direction = CCW;
  }
  if (direction == CW) {
#ifdef debug
    BT.print("Direction = ");
    BT.println("CW");
#endif
    goalAngle -= correction;
    drive(speed, speed, -speed, -speed);
  } else {
#ifdef debug
    BT.print("Direction = ");
    BT.println("CCW");
#endif
    goalAngle += correction;
    drive(-speed, -speed, speed, speed);
  }
  while (delta.rawValue() > gap || delta.rawValue() < -gap) {
    currentAngle = getGyroAngle();
    delta = goalAngle - currentAngle;
  }
  drive(0, 0, 0, 0);
  finishAngle = currentAngle;

#ifdef debug
  BT.print("Finish angle = ");
  BT.println(currentAngle.value());
  BT.print("Turn delta = ");
  BT.println(abs(startAngle.value() - currentAngle.value()));
  BT.print("Enc ticks = ");
  BT.println(main_counter);
  BT.println();
#endif
}

void testTurnAngle() {
  for (int i = 0; i < 4; i++) {
    delay(1000);
    turnByAngle(90, 40, 15.0, 2.0);
  }
  for (int i = 0; i < 4; i++) {
    delay(1000);
    turnByAngle(-90, 40, 13.0, 2.0);
  }
}

void testAlignment() {
  GyroAngle startAngle = getGyroAngle();
  float kg = 1.0;
  float offset = startAngle.rawValue();
  while(1) {
    driveAngle((startAngle - getGyroAngle()).rawValue());
  }
}