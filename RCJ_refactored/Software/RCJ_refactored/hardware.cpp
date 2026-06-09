#include <Arduino.h>
#include "hardware.hpp"
#include "config.hpp"
#include <Wire.h>


Servo Dribbler;

void initWire() {
  Wire.begin();
  // Wire.beginTransmission(Seeker);
}

void initDribbler() {
  Dribbler.writeMicroseconds(1000);
  Dribbler.attach(Dribbler_pin, 1000, 2000);
  Dribbler.writeMicroseconds(1000);
  // Dribbler.writeMicroseconds(2000);
  // delay(5000);
  // Dribbler.writeMicroseconds(1000);
  // delay(10000);
  // Dribbler.writeMicroseconds(1000);
}

void holdBall() {
  Dribbler.writeMicroseconds(1300);
}

void releaseBall() {
  Dribbler.writeMicroseconds(1000);
}

void initKicker() {
  pinMode(Kicker, OUTPUT);
  digitalWrite(Kicker, LOW);
}

void kick() {
  digitalWrite(Kicker, HIGH);
  delay(25);
  digitalWrite(Kicker, LOW);
}

void testKick() {
  while(!digitalRead(Key1));
  Serial.println("KICK!");
  delay(1000);
  kick();
}

