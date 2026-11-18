#include <Arduino.h>
#include "config_fw.hpp"
#include "motors_fw.hpp"
#include "gyro_fw.hpp"
#include "lines_fw.hpp"
#include "executors_fw.hpp"
#include "link_rx.hpp"
#include "link_tx.hpp"
#include "watchdog_fw.hpp"

// mega_fw — thin Arduino Mega 2560 executor firmware (Idea 12 / PROMPT_ROBOT §3).
//
// The Mega is ONLY an executor:
//   - receives drive commands from the Pi as binary T_DRIVE/T_STOP frames
//     (see PROTOCOL.md),
//   - applies the motors.cpp safety path (MotorInvert/limitPWM/MotorMinPWM),
//   - owns the MPU6050 DMP + drift-freeze at 100 Hz and streams heading
//     telemetry (with the still flag) on every fresh DMP packet,
//   - polls the 16 line sensors (mux + fast ADC) and streams RAW values,
//     streams the catcher microswitch state, heartbeats,
//   - executes the kicker pulse + dribbler servo bits,
//   - watchdog: no commands for 300 ms -> motors stop.
//
// NO strategy, NO ball parsing, NO gate parsing, NO line angle math.
// The Pi (robot_core) owns all of that.

// setupGPIO, ported from rcj hardware.cpp: ADC at 500 kHz (the line sensors
// are IR reflectance thresholds, not precision signals) + pin directions.
// The keys/encoders/screen of the reference are dropped (headless executor).
static void setupGPIO() {
  ADCSRA = (ADCSRA & ~((1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0)))
           | (1 << ADPS2) | (1 << ADPS0);
  for (int i = 0; i < 8; i++) {
    pinMode(Motors[i], OUTPUT);
  }
  for (int i = 0; i < 4; i++) {
    pinMode(Mux[i], OUTPUT);
  }
}

void setup() {
  Serial.begin(115200);          // USB debug console
  Serial2.begin(LinkBaud);       // the Pi link (/dev/serial0 on the Pi side)
  while (Serial2.available()) {
    Serial2.read();              // flush stale bytes from a previous run
  }
  setupGPIO();
  executors_setup();

  Serial.println("mega_fw: thin executor firmware (binary link v1)");
  status_led(true);              // LED on while the gyro comes up (rcj pattern)
  if (setupGyro()) {
    calibrateGyro();             // blocking; the robot must stand still
  } else {
    Serial.println("mega_fw: no MPU6050 - continuing without heading telemetry");
  }
  status_led(false);

  // No wait-for-key here (unlike rcj RCJ_NEW.ino): this firmware cannot move
  // anything until a valid Pi command arrives, and the watchdog stops
  // everything when the Pi goes silent. Safe to auto-start.

  // Prime the scheduler so nothing fires immediately at t=0.
  watchdog_feed();  // generous first window; the Pi is expected to start sending
  Serial.println("mega_fw: ready");
}

void loop() {
  // 1. Pi->Arduino: parse every pending byte; a completed valid frame applies
  //    the command and feeds the watchdog (inside link_rx).
  while (Serial2.available()) {
    parse_pi_byte((uint8_t)Serial2.read());
  }

  // 2. Watchdog check (performs the stop once when it trips).
  watchdog_update();

  // 3. Gyro: drain the DMP FIFO; a fresh packet -> heading frame (~100 Hz
  //    native rate, never synthetic).
  if (gyro_update()) {
    float h = gyro_heading();
    send_heading((int)(h * 100.0f), gyro_is_still());
  }

  // 4. Line sensors: raw poll at the configured rate -> raw values frame.
  static unsigned long lastLineMs = 0;
  unsigned long now = millis();
  if (now - lastLineMs >= LinePollMs) {
    lastLineMs = now;
    int raw[LineSensNum];
    lines_poll(raw);
    send_lines(raw);
  }

  // 5. Catcher microswitch at a low rate; the bit rides in the next T_LINE
  //    frame (PROTOCOL.md §4.2 — no separate catcher frame).
  static unsigned long lastCatcherMs = 0;
  if (now - lastCatcherMs >= CatcherLineMs) {
    lastCatcherMs = now;
    send_catcher(catcher_closed());
  }

  // 6. Heartbeat.
  static unsigned long lastHbMs = 0;
  if (now - lastHbMs >= HeartbeatMs) {
    lastHbMs = now;
    send_hb();
  }

  // 7. Kicker pulse end / dribbler servo bookkeeping (non-blocking).
  executors_update();

  // No delay(): the loop spins at kHz rate, DMP FIFO never overflows
  // (~0.24 s margin), Serial2 TX stays ahead of the line rate.
}
