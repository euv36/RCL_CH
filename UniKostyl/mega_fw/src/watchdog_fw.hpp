#pragma once
#include <Arduino.h>

// watchdog_fw — no valid command from the Pi for > WatchdogMs (300 ms,
// configurable in config_fw.hpp) -> wheels 0, kicker released, dribbler OFF.
// Telemetry keeps streaming so the Pi can see the Mega is alive.
// (PROTOCOL.md §5.)

// Called by link_rx on every VALID command frame (D or STOP).
void watchdog_feed();

// Called every loop iteration; performs the stop once when the timeout trips
// (not repeatedly). Returns true while the watchdog is in the tripped state.
bool watchdog_update();

// True when the watchdog has tripped and no command arrived since.
bool watchdog_tripped();
