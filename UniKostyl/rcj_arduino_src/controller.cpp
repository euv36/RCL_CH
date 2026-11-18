#include <Arduino.h>
#include "controller.hpp"
#include "ball.hpp"
#include "gyro.hpp"
#include "motors.hpp"
#include "gate.hpp"
#include "line.hpp"
#include "hardware.hpp"

// Uncomment to print the heading error / correction every 200 ms while
// driveDirection() runs. Useful to check that the gyro is actually working.
//  #define HEADING_DEBUG

GyroAngle baseAngle;

void alignBallGate() {
  baseSpeed = BS;
  int correctAngle = 60;
  ball.updateStatus();
  // GyroAngle currentAngle = getGyroAngle();
  if (ball.getAngle() < 0) {
    correctAngle = -60;
  }
  Serial.println("Aligning...");
  Serial.println(ball.getAngle());
  while (abs(ball.getAngle()) >= 5) {
    ball.updateStatus();
    gate.updateStatus();
    if (ball.getDistance() < 600) {
      baseSpeed = LS;
    } else {
      baseSpeed = BS;
    }
    checkLine();
    // Serial.println("Changing speed!");
    // Serial.println(ball.isFound());
    if (ball.isFound()) {
      // Serial.println(ball.getAngle() + correctAngle);
      // PD approach toward the ball: straight move + torque from error / damping.
      drivePD(GyroAngle(ball.getAngle() + correctAngle), 100, baseSpeed);
    }
    // currentAngle = getGyroAngle();
  }
  // Serial.println("Aliogned!!!");
  // drive(0, 0, 0, 0);
  // while(1);
}

void catchBall() {
  // GyroAngle currentAngle = getGyroAngle();
  baseSpeed = LS;
  while (!ball.isCatched() && abs(ball.getAngle()) < 10) {
    ball.updateStatus();
    gate.updateStatus();
    checkLine();
    // PD approach toward the ball.
    drivePD(GyroAngle(ball.getAngle()), 100, baseSpeed);
    // currentAngle = getGyroAngle();
  }
}



void score() {
  gate.updateStatus();
  while (gate.getArea() < 2000 && ball.isCatched()) {
    ball.updateStatus();
    gate.updateStatus();
    drivePD(GyroAngle(gate.getAngle()), 1000, BS);
    checkLine();
  }
  kick();
  // baseSpeed = BS;
}

int medianFilter(int a, int b, int c) {
  if ((a > b && a < c) || (a > c && a < b)) {
    return a;
  }
  if ((b > a && b < c) || (b > c && b < a)) {
    return b;
  }
  if ((c > a && c < b) || (c > b && c < a)) {
    return c;
  }
  // All three equal (or one of the comparisons was signed out): pick any.
  return c;
}

int pd(double error, float p, float d) {
  static int lastError = error;
  bool sign = error < 0;
  error = abs(error);
  int delta = error * p + (error - lastError) * d;
  lastError = error;
  if (sign) {
    return -delta;
  } else {
    return delta;
  }
}

void checkLine() {
  line.updateStatus();
  static int slowTime = millis();
  // if (line.getSensOnLine() >= 2) {
  if (line.isFirstCircleLine()) {
    driveFromLine();
  }
  if (line.isSecondCircleLine()) {
    // Serial.println("Going back!!!");
    driveFromLine();
    // Serial.print("Base speed = ");
    // Serial.println(baseSpeed);
  }
  // }
}

void driveFromLine() {
  // Serial.println(line.getAngle());
  // drive(0, 0, 0, 0);
  // while(1);
  // PD approach toward the ball / the position the line points at.
  drivePD(GyroAngle(line.getAngle() + 180), 30000, BS);
  baseSpeed = BS;
  while (line.getAngle() != -1) {
    line.updateStatus();
    // driveAngle(180 - line.getAngle());
  }
}

void play() {
#ifdef FORWARD
  if (abs(getGyroAngle().rawValue()) > 10.0) {
    turnToAngle(baseAngle, 45, 3.0, 10.0);
  }
  alignBallGate();
  catchBall();
  score();
#endif
#ifdef GOALKEEPER
  _goalkeeper_();
#endif
}

void goalkeeper() {
  int correctAngle = 30;
  ball.updateStatus();
  while (!ball.isCalm() || ball.isFound()) {
    ball.updateStatus();
    if (ball.getAngle() < 0) {
      correctAngle = -30;
    }
    Serial.println("Direction defined");
    long long startTime = millis();
    if (ball.getDistance() < BallTH) {
      while (millis() - startTime < 1500) {
        Serial.println("Defending!!!!!!");
        // PD approach/defense toward the ball.
        drivePD(GyroAngle(ball.getAngle() + correctAngle), 50, baseSpeed);
        checkLine();
        ball.updateStatus();
      }
      Serial.println("Defining gate direction...");
      if (gate.getAngle() > 0) {
        correctAngle = -30;
      } else {
        correctAngle = 30;
      }
      gate.updateStatus();
      while (abs(gate.getAngle()) > 10 && gate.getArea() < 1500) {
        Serial.println("Going home...");

        gate.updateStatus();
        // ball.updateStatus();
        // PD approach toward the gate while seeking the ball.
        drivePD(GyroAngle(gate.getAngle()), 100, baseSpeed);
        checkLine();
      }
      checkLine();
    }
    drive(0, 0, 0, 0);
  }
}


void goalkeeper_() {
  while (1) {
    ball.updateStatus();
    while (abs(ball.getAngle()) >= 10) {
      ball.updateStatus();
      gate.updateStatus();
      int delta = 180 - abs(gate.getAngle());
      if (ball.getAngle() > 0) {
        if (gate.getAngle() > 0) {
          drivePD(GyroAngle(90 + delta), 100, BS);
        } else {
          drivePD(GyroAngle(90 + delta), 100, BS);
        }
      } else {
        if (gate.getAngle() > 0) {
          drivePD(GyroAngle(-90 + delta), 100, BS);
        } else {
          drivePD(GyroAngle(-90 - delta), 100, BS);
        }
      }
      checkLine();
      drive(0, 0, 0, 0);
    }
  }
}

void _goalkeeper_() {
  while (1) {
    findGate();
    turnToBall();
    ball.updateStatus();
    long long startTime = millis();
    while (ball.getDistance() < BallTH && millis() - startTime < 500) {
      Serial.println("Defending!!!");
      // PD approach toward the ball.
      drivePD(GyroAngle(ball.getAngle()), 50, BS);
      ball.updateStatus();
      checkLine();
    }
    drive(0, 0, 0, 0);
    kick();
  }
}

void findGate() {
  gate.updateStatus();
  while(gate.getArea() < 3000 && gate.getArea() > 0) {
    ball.updateStatus();
    gate.updateStatus();
    // PD approach toward the gate found by the searching loop.
    drivePD(GyroAngle(gate.getAngle()), 100, baseSpeed);
    Serial.println("Searching gates...");
    checkLine();
  }
  Serial.println("GATE FOUND!!!");
  drive(0, 0, 0, 0);
} 

void turnToBall() {
  ball.updateStatus();
  Serial.println("Seeking ball...");
  // PD turn toward the ball: pure yaw torque from error - damping.
  GyroAngle target = getGyroAngle();
  target += GyroAngle(ball.getAngle());
  unsigned long startTime = millis();
  unsigned long lastTime = startTime;
  float lastAngle = getGyroAngle().rawValue();
  unsigned long lastRateTime = startTime;
  float yawRate = 0.0f;
  while (millis() - startTime < 3000) {
    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0f;
    if (dt <= 0.0f) dt = 0.001f;
    lastTime = now;
    float error = (target - getGyroAngle()).rawValue();
    if (now - lastRateTime >= 20) {
      float rateDt = (float)(now - lastRateTime) / 1000.0f;
      float rate = (getGyroAngle() - lastAngle).rawValue() / rateDt;
      yawRate = 0.5f * yawRate + 0.5f * rate;
      lastAngle = getGyroAngle().rawValue();
      lastRateTime = now;
    }
    float torque = headingKp * error - headingKd * yawRate;
    int angularSpeed = constrain((int)(headingInvert * torque), -30, 30);
    drive(angularSpeed, angularSpeed, -angularSpeed, -angularSpeed);
    if (abs(error) < 5.0f) {
      break;
    }
  }
  drive(0, 0, 0, 0);
}

// Moves in a fixed direction of the robot frame for `duration` ms while a PI
// loop on the gyro yaw keeps the heading captured at the start.
//
// The loop changes the individual wheel speeds (not just a "turn" value):
//   correction = headingInvert * (headingKp * error + headingKi * integral
//                                 - headingKd * yawRate)
// The left wheels get +correction, the right wheels -correction, so if the
// robot starts drifting the sides are sped up/slowed down until it is back on
// the start heading. The integral term removes the constant pull caused by a
// weak or draggy wheel, which is the usual reason it "does not go straight".
// The integral is kept between drive calls (one robot asymmetry pulls in the
// same direction in every segment), and the yaw-rate term damps the error so
// it does not just settle into a heading offset.
//
// moveAngle: 0 = forward, 180 = backward, +90 / -90 = sideways.
void driveDirection(int moveAngle, unsigned long duration, int speed) {
  // One command first: it releases the standstill freeze of the gyro, so the
  // heading below is the real current heading and not a stale frozen value.
  driveAngleWithRotation(moveAngle, 0, speed);
  GyroAngle heading = getGyroAngle();
  unsigned long startTime = millis();
  unsigned long lastTime = startTime;

  // Persistent heading integral (deg*s). Re-learning it from zero in every
  // ~1 s segment is why a square slowly rotated: one wheel asymmetry pulls in
  // the same direction every segment, but the integral was reset each time.
  static float headingIntegral = 0.0f;

  // For the four cardinal directions every wheel runs at 0.707 * speed, so
  // this is the base wheel PWM. The correction must stay below it, otherwise
  // a wheel would reverse and the motion stops being a straight line.
  int baseWheel = (int)(speed * 0.707f);
  int maxCorrection = (int)(baseWheel * headingMaxShare);
  if (maxCorrection < 1) {
    maxCorrection = 1;
  }

  // Yaw rate for the damping term, measured over at least 20 ms (the DMP yaw
  // updates at 100 Hz, shorter windows are pure noise).
  float lastAngle = heading.rawValue();
  unsigned long lastRateTime = startTime;
  float yawRate = 0.0f;

  while (millis() - startTime < duration) {
    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0f;
    if (dt <= 0.0f) {
      dt = 0.001f;
    }
    lastTime = now;

    // Wrapped to -180..180 by GyroAngle::operator-.
    float error = (heading - getGyroAngle()).rawValue();

    // Damping: push against the measured yaw rate so the error does not just
    // settle into a constant heading offset.
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

    // Anti-windup: integrate only while the correction was not saturated, so
    // a saturated step does not poison the next iterations.
    if (abs(correction) < maxCorrection) {
      headingIntegral += error * dt;
      headingIntegral = constrain(headingIntegral, -headingMaxIntegral, headingMaxIntegral);
    }

#ifdef HEADING_DEBUG
    static unsigned long lastPrint = 0;
    if (now - lastPrint >= 200) {
      lastPrint = now;
      Serial.print("heading err=");
      Serial.print(error);
      Serial.print(" int=");
      Serial.print(headingIntegral);
      Serial.print(" rot=");
      Serial.println(angularSpeed);
    }
#endif

    driveAngleWithRotation(moveAngle, angularSpeed, speed);
  }
  drive(0, 0, 0, 0);
}

void driveForward(unsigned long duration, int speed) {
  driveDirection(0, duration, speed);
}

void driveBackward(unsigned long duration, int speed) {
  driveDirection(180, duration, speed);
}

// If left and right are swapped on your robot, swap the +90 / -90 here.
void driveLeft(unsigned long duration, int speed) {
  driveDirection(-90, duration, speed);
}

void driveRight(unsigned long duration, int speed) {
  driveDirection(90, duration, speed);
}

// Pure-PD regulator for the approach to the ball / goal: drives the robot
// straight at `speed` and applies a yaw torque toward `target`:
//   torque = headingInvert * (headingKp * error - headingKd * yawRate)
//           error = target - current heading
// No integral term - the approach is short, error + damping are enough and it
// keeps the robot from drifting. This is the ball-approach controller.
void drivePD(GyroAngle target, unsigned long duration, int speed) {
  int baseWheel = (int)(speed * 0.707f);
  int maxCorrection = (int)(baseWheel * headingMaxShare);
  if (maxCorrection < 1) maxCorrection = 1;

  GyroAngle heading = getGyroAngle();
  unsigned long startTime = millis();
  unsigned long lastTime = startTime;
  float lastAngle = heading.rawValue();
  unsigned long lastRateTime = startTime;
  float yawRate = 0.0f;

  while (millis() - startTime < duration) {
    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0f;
    if (dt <= 0.0f) dt = 0.001f;
    lastTime = now;

    float error = (target - getGyroAngle()).rawValue();
    if (now - lastRateTime >= 20) {
      float rateDt = (float)(now - lastRateTime) / 1000.0f;
      float rate = (getGyroAngle() - lastAngle).rawValue() / rateDt;
      yawRate = 0.5f * yawRate + 0.5f * rate;
      lastAngle = getGyroAngle().rawValue();
      lastRateTime = now;
    }
    float torque = headingKp * error - headingKd * yawRate;
    int angularSpeed = constrain((int)(headingInvert * torque), -maxCorrection, maxCorrection);

    driveAngleWithRotation(0, angularSpeed, speed);
  }
  drive(0, 0, 0, 0);
}

// Turns in place (pure yaw torque) by `angle` degrees using the gyro, without
// overshooting. Same PD torque as the ball approach, in-place variant.
void turnInPlace(int angle, int speed) {
  GyroAngle target = getGyroAngle();
  target += GyroAngle(angle);
  GyroAngle error = target - getGyroAngle();
  unsigned long startTime = millis();
  while (abs(error.rawValue()) > 2.0 && millis() - startTime < 5000) {
    int rotation = abs(error.rawValue()) * 2;
    rotation = constrain(rotation, 30, speed);
    if (error.rawValue() < 0) {
      rotation = -rotation;
    }
    drive(rotation, rotation, -rotation, -rotation);
    error = target - getGyroAngle();
  }
  drive(0, 0, 0, 0);
  delay(100);
}