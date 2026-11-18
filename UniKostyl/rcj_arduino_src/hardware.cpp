#include <Arduino.h>
#include <MPU6050_6Axis_MotionApps20.h>
#include <Servo.h>
#include "hardware.hpp"
#include "Wire.h"
#include "config.hpp"

Servo Dribbler;

void setupGPIO() {
  // Speed up the ADC (default ~125 kHz). The line sensors are IR reflectance
  // thresholds, not precision signals, so 500 kHz is plenty and ~4.5x faster.
  ADCSRA = (ADCSRA & ~((1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0)))
           | (1 << ADPS2) | (1 << ADPS0);
  for (int i = 0; i < 8; i++) {
    pinMode(Motors[i], OUTPUT);
  }
  for (int i = 0; i < 4; i++) {
    pinMode(Mux[i], OUTPUT);
  }
  pinMode(Kicker, OUTPUT);
  pinMode(LeftKey, INPUT_PULLUP);
  pinMode(RightKey, INPUT_PULLUP);
  pinMode(EncA, INPUT_PULLUP);
  pinMode(EncB, INPUT_PULLUP);
  pinMode(EncKey, INPUT_PULLUP);
  pinMode(Ball_detector, INPUT);
}

void setupWire() {
  Wire.begin();
}

void setupSerial(){
  Serial.begin(115200);
}

bool setupGyro() {
  // const int16_t accel_offset[3] = { -1764, 319, 803 };
  // const int16_t gyro_offset[3] = { 110, 6, 34 };
#ifdef FORWARD
  const int16_t accel_offset[3] = { -2961, 439, 1193};
  const int16_t gyro_offset[3] = { 77, 90, -30};
#endif

#ifdef GOALKEEPER
  const int16_t accel_offset[3] = { -3531, -1223, 1339 };
  const int16_t gyro_offset[3] = { -1872, 13, -35};
#endif
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

void setupScreen() {

}

void setupDribbler() {
  Dribbler.writeMicroseconds(1000);
  Dribbler.attach(Dribbler_pin, 1000, 2000);
  Dribbler.writeMicroseconds(1000);
  // Dribbler.writeMicroseconds(2000);
  // delay(5000);
  // Dribbler.writeMicroseconds(1000);
  // delay(10000);
  // Dribbler.writeMicroseconds(1000);
}

void setupRPi() {
  piSerial.begin(115200);
  while (piSerial.available()) {
    piSerial.read();
  }
}

void setupSysTick() {
}

void kick() {
  static long long cooldown = millis();
  if (millis() - cooldown > 1000) { 
    digitalWrite(Kicker, HIGH);
    delay(40);
    digitalWrite(Kicker, LOW);
    cooldown = millis();
  }
}

void startDribbler() {

}

void stopDribbler() {

}
