#pragma once
#include <Arduino.h>

// executors_fw — kicker / dribbler / catcher / status LED.
// The kicker is NON-BLOCKING (the rcj kick() used delay(40) inside the control
// loop; that would break the >100 Hz loop and the telemetry rate here).

// Pin modes + servo attach (setup).
void executors_setup();

// Applies the kicker request bit. The Mega owns pulse width + cooldown: a
// request with the cooldown still running is ignored (logged on USB debug).
void kicker_request(bool fire);

// Kicker state for diagnostics: pulse currently high.
bool kicker_active();

// Applies the dribbler bit: ON -> Dribbler_us_on servo position,
// OFF -> Dribbler_us_off. Idempotent (no servo churn on repeated bits).
void dribbler_set(bool on);

// Catcher microswitch state, normalized: true = ball held (pin active LOW).
bool catcher_closed();

// Must be called every loop iteration: ends the kicker pulse when its 40 ms
// elapsed (non-blocking timing).
void executors_update();

// Status LED (moved off pin 16, see config_fw.hpp): solid on/off.
void status_led(bool on);

// Emergency: cut any kicker pulse in progress immediately (STOP/watchdog).
void kicker_release();
