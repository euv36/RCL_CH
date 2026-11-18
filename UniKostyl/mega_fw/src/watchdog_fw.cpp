#include <Arduino.h>
#include "watchdog_fw.hpp"
#include "motors_fw.hpp"
#include "executors_fw.hpp"
#include "config_fw.hpp"

static unsigned long lastFeedMs = 0;
static bool fed = false;
static bool tripped = false;

void watchdog_feed() {
  lastFeedMs = millis();
  fed = true;
  tripped = false;
}

bool watchdog_update() {
  unsigned long now = millis();
  if (fed && !tripped && (now - lastFeedMs > WatchdogMs)) {
    // No commands from the Pi: stop everything (the robot must not go insane
    // when the Pi drops out). Telemetry continues; the Pi sees the Mega is
    // alive but silent-command state via its own telemetry-stale degradation.
    motorsStop();
    kicker_release();
    dribbler_set(false);
    tripped = true;
    Serial.println("WATCHDOG: no Pi commands, motors stopped");
  }
  return tripped;
}

bool watchdog_tripped() {
  return tripped;
}
