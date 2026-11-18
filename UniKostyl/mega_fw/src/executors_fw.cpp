#include <Arduino.h>
#include <Servo.h>
#include "executors_fw.hpp"
#include "config_fw.hpp"

// Kicker timing ported from rcj hardware.cpp kick(): 40 ms HIGH pulse,
// cooldown timer starts after the pulse ends (start-to-start ≈ 1040 ms).
// Made non-blocking: the pulse end is scheduled, no delay() in the loop.

static Servo Dribbler;
static bool dribblerAttached = false;
static bool dribblerState = false;

static bool pulseActive = false;
static unsigned long pulseStartMs = 0;
static unsigned long cooldownEndMs = 0;   // millis() after which a new pulse may start

void executors_setup() {
  pinMode(Kicker, OUTPUT);
  digitalWrite(Kicker, LOW);
  pinMode(Catcher_pin, INPUT);            // rcj: Ball_detector, INPUT (not pullup)
  pinMode(StatusLed, OUTPUT);
  digitalWrite(StatusLed, LOW);
  Dribbler.writeMicroseconds(Dribbler_us_off);
  Dribbler.attach(Dribbler_pin, Dribbler_us_off, Dribbler_us_on);
  Dribbler.writeMicroseconds(Dribbler_us_off);
  dribblerAttached = true;
  dribblerState = false;
}

void kicker_request(bool fire) {
  if (!fire) {
    return;
  }
  unsigned long now = millis();
  if (pulseActive) {
    return;  // pulse already running
  }
  if (now < cooldownEndMs) {
#if FW_DEBUG_TEXT
    Serial.println("kicker: cooldown, request ignored");
#endif
    return;
  }
  digitalWrite(Kicker, HIGH);
  pulseStartMs = now;
  pulseActive = true;
}

void executors_update() {
  if (pulseActive && millis() - pulseStartMs >= KickerPulseMs) {
    digitalWrite(Kicker, LOW);
    pulseActive = false;
    cooldownEndMs = millis() + KickerCooldownMs;   // rcj: timer starts after the pulse
  }
}

bool kicker_active() {
  return pulseActive;
}

void dribbler_set(bool on) {
  if (!dribblerAttached || on == dribblerState) {
    return;
  }
  Dribbler.writeMicroseconds(on ? Dribbler_us_on : Dribbler_us_off);
  dribblerState = on;
}

bool catcher_closed() {
  // rcj checkIfCatched(): digitalRead == 0 -> catched (active LOW switch).
  return digitalRead(Catcher_pin) == LOW;
}

void status_led(bool on) {
  digitalWrite(StatusLed, on ? HIGH : LOW);
}

// Emergency path used by the watchdog/STOP: release the pulse immediately.
void kicker_release() {
  digitalWrite(Kicker, LOW);
  pulseActive = false;
}
