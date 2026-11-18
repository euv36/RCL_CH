#include <Arduino.h>
#include "debug.hpp"
#include "ball.hpp"
#include "line.hpp"
#include "gyro.hpp"
#include "motors.hpp"
#include "hardware.hpp"
#include "config.hpp"
#include "controller.hpp"
#include "gate.hpp"

// Reads the line sensors only (no motor output) and prints raw values, binary
// values and the computed angle at 100 Hz. Use this to calibrate
// lineSensorsTH[] (line.hpp): put the robot on the line, leave it still and
// watch the numbers. Blocks forever; enable at most one debug helper in
// loop() at a time.
void printLineSensors() {
  unsigned long lastPrint = 0;
  while (1) {
    line.updateStatus();
    unsigned long now = millis();
    if (now - lastPrint < 100) {
      delay(5);
      continue;
    }
    lastPrint = now;
    int* raw = line.getRawValues();
    bool* bin = line.getBinValues();
    Serial.print("raw: ");
    for (int i = 0; i < LineSensNum; i++) {
      Serial.print(raw[i]);
      Serial.print(' ');
    }
    Serial.print(" bin: ");
    for (int i = 0; i < LineSensNum; i++) {
      Serial.print(bin[i] ? 1 : 0);
      Serial.print(' ');
    }
    Serial.print(" angle=");
    Serial.print(line.getAngle());
    Serial.print(" c1=");
    Serial.print(line.isFirstCircleLine() ? 1 : 0);
    Serial.print(" c2=");
    Serial.println(line.isSecondCircleLine() ? 1 : 0);
  }
}

void printLine() {
  line.updateStatus();
  int angle = rand() % 360 - 180;
  driveAngle(angle);
  int* rawLine = line.getRawValues();
  bool* binLine = line.getBinValues();
  Serial.println("/////////////////////////////////////////////////////////////////////////////////////////");
  Serial.print("First circle: ");
  for (int i = 0; i < LineSensNum; i += 2) {
    Serial.print(rawLine[i]);
    Serial.print(" ");
  }
  Serial.print(" | ");
  for (int i = 0; i < LineSensNum; i += 2) {
    Serial.print(binLine[i]);
    Serial.print(" ");
  }
  Serial.println((line.isFirstCircleLine()) ? "Slowing down!!!" : "No line in first circle");
  Serial.print("Second circle: ");
  for (int i = 1; i < LineSensNum; i += 2) {
    Serial.print(rawLine[i]);
    Serial.print(" ");
  }
  Serial.print(" | ");
  for (int i = 1; i < LineSensNum; i += 2) {
    Serial.print(binLine[i]);
    Serial.print(" ");
  }
  Serial.println((line.isSecondCircleLine()) ? "Going back!!!" : "No line in second circle");
  Serial.print("Angle = ");
  Serial.println(line.getAngle());
  delay(500);
}

void printBall() {
  while (1) {
    ball.updateStatus();
    Serial.print("Angle = ");
    Serial.println(ball.getAngle());
    Serial.print("Strength = ");
    Serial.println(ball.getStrength());
    Serial.print("Distance = ");
    Serial.println(ball.getDistance());
    Serial.print("Tangent angle = ");
    Serial.println(ball.getTangentAngle());
    Serial.print("Found = ");
    Serial.println((ball.isFound()) ? "found" : "NOT FOUND!");
    Serial.print("Catched = ");
    Serial.println((ball.isCatched()) ? "CATCHED!" : "not catched");
    Serial.print("Calm = ");
    Serial.println((ball.isCalm()) ? "CALM!\n" : "not calm\n");
    delay(600);
  }
}

void printGyro() {
  GyroAngle angle;
  unsigned long lastPrint = 0;
  while (1) {
    // Call often so the DMP FIFO is drained; it overflows after ~0.24 s and
    // then the reported raw yaw jumps around. Print only every 500 ms.
    angle = getGyroAngle();
    unsigned long now = millis();
    if (now - lastPrint < 500) {
      delay(5);
      continue;
    }
    lastPrint = now;
    Serial.print("angle=");
    Serial.print(angle.rawValue(), 2);
    Serial.print("  raw=");
    Serial.print(getRawYaw(), 2);
    Serial.print("  drift=");
    Serial.print(getGyroBias(), 3);
    Serial.print("  still=");
    Serial.println(gyroIsStill() ? "yes" : "no");
  }
}

void rawTestDrive() {
  drive(100, 0, 0, 0);
  delay(2000);
  drive(0, 100, 0, 0);
  delay(2000);
  drive(0, 0, 100, 0);
  delay(2000);
  drive(0, 0, 0, 100);
  delay(2000);
}

void testKick() {
  while(!digitalRead(LeftKey));
  Serial.println("KICK!");
  kick();
}

void followBall() {
  while(1) {
    ball.updateStatus();
    driveAngle(ball.getAngle());
  }
}

void followBallWithRotation() {
  while(1) {
    ball.updateStatus();
    driveAngleWithRotation(ball.getAngle(), pd(ball.getAngle(), p, d));
  }
}

void debugBallDetector() {
  Serial.println(digitalRead(Ball_detector));
  delay(200);
}

void printGateAngle() {
  gate.updateStatus();
  Serial.print("Angle = ");
  Serial.println(gate.getAngle());
  Serial.print("Area = ");
  Serial.println(gate.getArea());
  delay(200);
}

void followGate() {
  gate.updateStatus();
  driveAngle(gate.getAngle());
}

// DVD-bounce demo: the robot drives at a fixed motion axis; whenever the line
// is in view (i.e. it is about to reach the wall) it flips the motion vector
// 180 deg and moves away from it - like a DVD logo bouncing off the screen.
// Straightness for both directions is held by the gyro (same PD law as
// driveDirection(), without the integral term). Fires once per entry to the
// line with a short cooldown, so it does not keep flipping while touching it.
void debugLine() {
  int angle = 0;                    // motion axis: 0 = forward, 180 = backward
  bool onLine = false;
  unsigned long lastBounce = 0;
  GyroAngle heading = getGyroAngle();

  // Gyro heading-hold; same PD law as driveDirection() + the persistent
  // heading integral, which is what keeps an asymmetric-wheeled robot from
  // weaving. The integral is static so it stays between drive calls.
  int baseWheel = (int)(BS * 0.707f);
  int maxCorrection = (int)(baseWheel * headingMaxShare);
  if (maxCorrection < 1) {
    maxCorrection = 1;
  }
  static float headingIntegral = 0.0f;
  float lastAngle = heading.rawValue();
  unsigned long lastRateTime = millis();
  unsigned long lastTime = millis();
  float yawRate = 0.0f;

  driveAngleWithRotation(angle, 0, BS);
  while (1) {
    line.updateStatus();
    // React on both sensor circles (+ the computed angle) so the line is
    // spotted earlier.
    bool lineNow = line.isFirstCircleLine() || line.isSecondCircleLine()
                   || line.getAngle() != -1;
    if (lineNow) {
      if (!onLine && millis() - lastBounce > 300) {
        angle = (angle + 180) % 360;
        // Note: the heading is NOT re-captured here. The robot reverses along
        // the same axis it came; the gyro keeps that very heading straight.
        onLine = true;
        lastBounce = millis();
        Serial.println("bounce!");
      }
    } else {
      onLine = false;
    }

    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0f;
    if (dt <= 0.0f) {
      dt = 0.001f;
    }
    lastTime = now;
    float error = (heading - getGyroAngle()).rawValue();
    if (now - lastRateTime >= 20) {
      float rateDt = (float)(now - lastRateTime) / 1000.0f;
      float rate = (getGyroAngle() - lastAngle).rawValue() / rateDt;
      yawRate = 0.5f * yawRate + 0.5f * rate;
      lastAngle = getGyroAngle().rawValue();
      lastRateTime = now;
    }
    float rawCorrection = headingKp * error + headingKi * headingIntegral - headingKd * yawRate;
    float correction = headingInvert * rawCorrection;
    int angularSpeed = constrain((int)correction, -maxCorrection, maxCorrection);
    // Anti-windup: integrate only while the correction is not saturated.
    if (abs(correction) < maxCorrection) {
      headingIntegral += error * dt;
      headingIntegral = constrain(headingIntegral, -headingMaxIntegral, headingMaxIntegral);
    }
    driveAngleWithRotation(angle, angularSpeed, BS);
    delay(20);
  }
}

void followGateWithRotation() {
  gate.updateStatus();
  driveAngleWithRotation(gate.getAngle(), pd(gate.getAngle(), gp, gd));
}

// Spins the robot in place with a fixed positive rotation command for ~1.5 s
// and reads the yaw change. The verdict tells whether a positive angularSpeed
// really increases the gyro yaw (headingInvert = 1) or decreases it
// (headingInvert = -1). Put the robot on the floor - it will turn in place.
void headingSignTest() {
  Serial.println("headingSignTest: rotating for 1.5 s...");
  drive(0, 0, 0, 0);
  delay(300);
  GyroAngle start = getGyroAngle();
  drive(40, 40, -40, -40);
  delay(1500);
  drive(0, 0, 0, 0);
  delay(300);
  float delta = (getGyroAngle() - start).rawValue();
  Serial.print("headingSignTest: yaw delta = ");
  Serial.println(delta, 2);
  if (delta > 5.0f) {
    Serial.println("VERDICT: positive rotation INCREASES the yaw -> keep headingInvert = 1");
  } else if (delta < -5.0f) {
    Serial.println("VERDICT: positive rotation DECREASES the yaw -> set headingInvert = -1");
  } else {
    Serial.println("VERDICT: yaw barely moved -> the rotation command does not turn the robot, check the wheel mapping");
  }
}

// All four wheels forward at the same raw PWM. No gyro, no direction mixing.
// Put the robot on the floor: it MUST drive straight forward. If it goes
// diagonally or curves hard, a wheel is wired/mapped the wrong way or is much
// weaker - fix it with MotorInvert[] in config.hpp, not by swapping pins.
void rawForward() {
  Serial.println("rawForward: all wheels +80 for 3 s");
  drive(80, 80, 80, 80);
  delay(3000);
  drive(0, 0, 0, 0);
  Serial.println("rawForward done");
}

// Spins one wheel at a time at +80 for 1.5 s and prints its name, so you can
// see which physical wheel turns and in which direction.
// Lift the robot off the floor before running this.
void rawTestWheels() {
  const char* names[4] = {"LF", "LB", "RF", "RB"};
  for (int i = 0; i < 4; i++) {
    Serial.print("Test ");
    Serial.print(names[i]);
    Serial.println(" forward");
    int s[4] = {0, 0, 0, 0};
    s[i] = 80;
    drive(s[0], s[1], s[2], s[3]);
    delay(1500);
    drive(0, 0, 0, 0);
    delay(700);
  }
  Serial.println("rawTestWheels done");
}