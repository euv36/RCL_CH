# PROTOCOL.md — Pi ↔ Arduino Mega serial protocol (Idea 12 / PROMPT_ROBOT §6)

> ## ✅ APPROVED — v1 (2026-11-18)
>
> The user approved DRAFT v1 in full (all 8 checklist items in §8). The binary
> framing below is now the implemented wire format on both sides:
> `mega_fw/src/link_frame.hpp` (shared CRC + RX scanner), `mega_fw/src/link_rx.cpp`,
> `mega_fw/src/link_tx.cpp`, and `robot_core/src/link_codec.*` (wrapped by
> `uart_link_linux.*`). The interim text format in §7 is retired.
>
> Status: **APPROVED v1 (2026-11-18). Implemented on both sides.** Verified by
> PC conformance tests (`mega_fw/tests/test_link_frame.cpp`,
> `robot_core/tests/test_logic.cpp`) and a PlatformIO compile of `mega_fw`.
> Not yet run against real hardware.

---

## 1. Scope and channel

- Link: Raspberry Pi `/dev/serial0` ↔ Mega 2560 `Serial2` (TX2 = pin 16, RX2 = pin 17), **115200 8N1**.
- Full duplex; the two directions are independent (separate wires), so the bandwidth budgets below are per direction.
- Effective capacity at 115200 baud, 10 bits/byte: **11520 bytes/s per direction**.
- Purpose: Pi sends drive commands; Mega streams telemetry. This replaces the legacy one-way camera `G<id>,<angle>,<area>` link for the NEW pair only — the old pair (`uart_rpi.ino` + `rpi_elp_cam.py`) is untouched and stays as rollback.

### Who owns what (context for the field definitions)

| Side | Owns |
|---|---|
| Mega (executor) | wheel H-bridges + `MotorInvert[]`/`MotorPWM` cap/`MotorMinPWM[]` safety path, MPU6050 DMP + drift-freeze heading, mux + raw ADC of 16 line sensors, kicker pulse timing + cooldown, dribbler servo position, catcher microswitch, watchdog |
| Pi (`robot_core`) | omni mixing (motion angle/speed/rotation → 4 wheel PWM), PI/PD heading laws closed on the gyro telemetry, all tactics, line angle computation, ball Locator (I2C 0x0E on the Pi bus), camera gate status |

## 2. Framing choice: fixed binary + CRC8 (approved)

Two candidate framings were evaluated.

### Option A — newline-delimited text lines (what the interim formats use)

Each message is one `\n`-terminated line of comma-separated decimal fields.
- **Pros:** human-readable on any serial monitor; self-delimiting; trivially extensible; no framing state machine.
- **Cons (deciding):**
  1. **Lost-byte safety is weak.** A dropped byte merges two lines into one (two frames lost) or, worse, shifts digits *within* a line: `H,-12345,1` losing a digit becomes `H,-1235,1` — a plausible heading. Text has no integrity check; corrupted-but-plausible values go straight into motor PWMs and heading hold. Catching this needs a checksum field, at which point the line is no longer human-readable anyway.
  2. **Bandwidth at the required telemetry rate is tight.** A text line with heading + still + 16 raw ADC values (0..1023, up to 4 digits + separator each) is ≥ 78 chars → ≥ 780 bytes/s at 100 Hz ≈ **68 % of the channel**, leaving no headroom; sending it per fresh DMP packet leaves less than ~35 % for everything else and for bursts.
  3. **Parse cost on the Mega at 100 Hz.** Formatting/parsing 17 integers per 10 ms (with `snprintf`/`strtol`) is affordable but not free; the binary frame is a fixed-offset struct read + CRC.

### Option B — fixed-size binary frames + CRC8 (**approved**)

Fixed-length frames, one sync byte, one type byte, one sequence number, little-endian payload, CRC-8 (ATM poly `0x07`, init `0x00`, computed over type+seq+payload, sync excluded).
- **Lost-byte safety:** the CRC rejects every single-bit and most multi-bit corruptions — including the plausible-digit-shift case that silently passes as text. A dropped byte kills exactly one frame; the fixed frame length lets the RX state machine resync deterministically on the next sync byte. A corrupted *length-less* text stream cannot resync at all.
- **Bandwidth:** full telemetry ≈ 2.7 kB/s (§4), **~24 % of the channel** — headroom for future additive messages (encoder ticks, board states for Ideas 10/11, …).
- **Parse cost:** RX = byte-level state machine + one CRC8 pass (13 bytes → ~104 bit-ops on the Mega, tens of µs at 16 MHz); TX = struct pack + CRC. Cheaper than text parsing, which matters because the Mega loop must stay > 100 Hz *with* line polling (48 ADC conversions ≈ 1.3 ms per poll) and the DMP FIFO drain included.
- **Cons:** not human-readable → mitigated by a compile-time `FW_DEBUG_TEXT` mode in `mega_fw` that mirrors frames as text on the USB `Serial` console (never on the link), and by `robot_core --verbose` frame dumps. Slightly more code on both sides (one small state machine each).

**Decision (2026-11-18): Option B.** The controlling argument is failure semantics: the dangerous failure mode is not "frame lost" (one 10 ms gap, watchdog-safe) but "value silently wrong" — only a checksummed framing rejects that. Option B additionally halves the telemetry bandwidth vs text and reduces Mega-side parse cost.

## 3. Common framing rules (Option B)

- Byte order: little-endian (AVR native).
- Multi-byte signed values: two's complement `int16`.
- Every frame: `SYNC(0xA5) | TYPE | SEQ | payload... | CRC8`.
  - `SEQ`: uint8, incremented per frame *per direction*; used for drop diagnostics/logging only (safety comes from CRC + watchdog, not from SEQ).
  - `CRC8`: ATM polynomial `0x07`, init `0x00`, no reflection, computed over `TYPE..payload` (SYNC excluded).
- A frame with bad CRC or unknown TYPE is dropped (counted); the RX resyncs by scanning for `0xA5` + known TYPE. Since all frames have fixed lengths, resync after a corrupt stream is O(1) per candidate.
- Both sides keep RX buffers small (a frame never exceeds 40 bytes) and never block the loop waiting for the link.

### CRC8 reference implementation (both sides must match bit-for-bit)

```c
uint8_t crc8_atm(const uint8_t* data, size_t n) {   // poly 0x07, init 0x00
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}
```

## 4. Frame catalog

### 4.1 Pi → Arduino

#### `T_DRIVE` (type `0x01`, 13 bytes, up to 100 Hz)

The periodic drive command. Sent every control tick (50–100 Hz) while the Pi wants motion; may also be sent at lower rate when idle. **Its mere arrival resets the Mega watchdog.**

| Offset | Size | Field | Type | Values / meaning |
|---|---|---|---|---|
| 0 | 1 | SYNC | `0xA5` | frame marker |
| 1 | 1 | TYPE | `0x01` | drive command |
| 2 | 1 | SEQ | uint8 | rolling, per direction |
| 3 | 2 | `pwm_lf` | int16 LE | left-front wheel PWM, −255..255, **before** `MotorInvert[]` (see §6) |
| 5 | 2 | `pwm_lb` | int16 LE | left-back wheel PWM, −255..255 |
| 7 | 2 | `pwm_rf` | int16 LE | right-front wheel PWM, −255..255 |
| 9 | 2 | `pwm_rb` | int16 LE | right-back wheel PWM, −255..255 |
| 11 | 1 | FLAGS | uint8 | bit0 `kicker_fire` (request), bit1 `dribbler_on`, bits2..7 reserved 0 |
| 12 | 1 | CRC8 | uint8 | over offsets 1..11 |

Wheel order everywhere is **LF, LB, RF, RB** (the reference firmware's order). Signs: positive = the wheel's "forward" as in the reference `drive()` (i.e. before `MotorInvert[]` is applied on the Mega). The Pi clamps to ±255 and pre-scales to the `MotorPWM` cap; the Mega applies its safety path again (idempotent — see §6).

#### `T_STOP` (type `0x02`, 4 bytes, on demand)

Explicit emergency stop. Idempotent; may be repeated.

| Offset | Size | Field | Type | Values / meaning |
|---|---|---|---|---|
| 0 | 1 | SYNC | `0xA5` | |
| 1 | 1 | TYPE | `0x02` | stop |
| 2 | 1 | SEQ | uint8 | |
| 3 | 1 | CRC8 | uint8 | over offset 1..2 |

Effect on the Mega: all four wheels to 0 immediately, kicker released (any pulse in progress cut short), dribbler to OFF position, watchdog reset. Telemetry continues. (No payload: nothing configurable about "stop".)

*(Reserved for later, NOT in this draft's implementation scope: `T_CONFIG` for runtime-tunable watchdog/cooldown values. Keeping the frame catalog minimal until the base link is accepted.)*

### 4.2 Arduino → Pi

#### `T_HDG` (type `0x11`, 8 bytes, ≈100 Hz — sent on every fresh DMP packet)

Heading telemetry. One frame per consumed DMP FIFO packet ⇒ native ~100 Hz, never faster than the sensor (no synthetic padding).

| Offset | Size | Field | Type | Values / meaning |
|---|---|---|---|---|
| 0 | 1 | SYNC | `0xA5` | |
| 1 | 1 | TYPE | `0x11` | heading |
| 2 | 1 | SEQ | uint8 | |
| 3 | 2 | `heading_cdeg` | int16 LE | heading in **centidegrees**, wrapped to −18000..18000 (−180.00..180.00°), reference-frame = DMP yaw exactly as the reference `getGyroAngle()` reports it (drift-freeze applied) |
| 5 | 1 | FLAGS | uint8 | bit0 `still` (heading currently frozen by the drift-freeze logic), bits1..7 reserved 0 |
| 6 | 1 | reserved | uint8 | 0 |
| 7 | 1 | CRC8 | uint8 | over offsets 1..6 |

Angle convention: matches the reference firmware — degrees, −180..180, positive = the DMP yaw sign (CCW looking from above, same sign family as the camera angle convention 0°=right/CCW; the π offset between camera and robot frames is handled in `robot_core`, not on the wire).

#### `T_LINE` (type `0x12`, 38 bytes, 50 Hz)

Raw line-sensor telemetry. **Raw ADC values only — the Mega computes no thresholds and no angle** (that logic moves to the Pi per the split).

| Offset | Size | Field | Type | Values / meaning |
|---|---|---|---|---|
| 0 | 1 | SYNC | `0xA5` | |
| 1 | 1 | TYPE | `0x12` | lines |
| 2 | 1 | SEQ | uint8 | |
| 3 | 32 | `raw[16]` | 16 × uint16 LE | raw ADC 0..1023, sensor index 0..15 in the reference `lineSensorPos` order (the Pi applies the same mux-position table, so indices are meaningful) |
| 35 | 1 | FLAGS | uint8 | bit0 `catcher` (catcher microswitch CLOSED = 1; pin 14 is active-LOW, the Mega normalizes), bits1..7 reserved 0 |
| 36 | 1 | reserved | uint8 | 0 |
| 37 | 1 | CRC8 | uint8 | over offsets 1..36 |

50 Hz (every mux poll). The catcher bit rides here so the Pi gets it at the line rate; no separate catcher frame.

#### `T_HB` (type `0x13`, 4 bytes, 2 Hz / every 500 ms)

Heartbeat: "firmware alive" even if the DMP died or the mux poll stalled.

| Offset | Size | Field | Type | Values / meaning |
|---|---|---|---|---|
| 0 | 1 | SYNC | `0xA5` | |
| 1 | 1 | TYPE | `0x13` | heartbeat |
| 2 | 1 | SEQ | uint8 | |
| 3 | 1 | CRC8 | uint8 | over offsets 1..2 |

### 4.3 Bandwidth budget (per direction)

| Direction | Frames | Rate | Bytes/s | % of 11520 |
|---|---|---|---|---|
| Pi→Mega | `T_DRIVE` 13 B | 100 Hz | 1300 | 11 % |
| Mega→Pi | `T_HDG` 8 B @100 Hz + `T_LINE` 38 B @50 Hz + `T_HB` 4 B @2 Hz | — | 800 + 1900 + 8 ≈ 2708 | 24 % |

Headroom ≈ 8.5 kB/s each way for future additive messages (Idea 10/11 reverse-channel data: encoder ticks, phototransistor board states — these would be new TYPEs, never a change to existing frames).

## 5. Watchdog (Mega side)

- `WATCHDOG_MS` = **300 ms** (compile-time configurable in `config_fw.hpp`).
- Any valid `T_DRIVE` or `T_STOP` frame resets the timer.
- If no valid command frame arrives for > `WATCHDOG_MS`:
  1. all four wheels → 0 (through the same safety path),
  2. kicker released, dribbler → OFF,
  3. the Mega keeps streaming telemetry (so the Pi can see it is alive) and logs the watchdog trip on the USB debug console.
- Rationale: the robot must not "go insane" when the Pi crashes, hangs, or the cable falls out. 300 ms ≈ 30 missed ticks at 100 Hz — long enough to survive normal scheduling jitter, short enough that the robot coasts < a few cm at match speeds.
- The Pi-side FSM also degrades if *telemetry* (any Mega frame) is silent > `telemetry_stale_ms` (default 300 ms): it stops sending new drive commands (a stopped Pi loop plus the Mega watchdog is two independent kill paths).

## 6. Value handling rules (both sides)

1. **PWM clamp:** the Pi clamps each wheel to ±255 and applies the reference proportional cap (`limitPWM` → max wheel = `MotorPWM` = 70) and the `MotorMinPWM[]` floor, mirroring `rcj_arduino_src/motors.cpp`. The Mega applies `MotorInvert[]` → ±255 constrain → `limitPWM` → `MotorMinPWM[]` again; all steps are idempotent, so double application is a no-op. The Mega path stays authoritative (it owns the real pins).
2. **Angle wrap:** every heading delta on the Pi is wrapped to −180..180 (359→1 is 2°, not 358). On the wire, heading is already wrapped to −18000..18000 centidegrees.
3. **Stale sensors:** a Mega telemetry gap > `telemetry_stale_ms` or a Locator I2C failure → the Pi keeps the last value only for the stale timeout, then degrades (ball-only/line-only/stop), logs the reason, never blocks.
4. **Never block:** no busy-wait on either side; the Mega loop has no `delay()` in the steady state; the Pi control tick is a fixed-rate sleep-until loop.

## 7. Retired interim text format (bench history only)

> The checksum-free text grammar below was used only to smoke-test the skeleton
> before approval. **It has been removed from both sides and must not be used in
> a match.** Kept for history/rollback reference only.

Interim Pi→Arduino (one line per command, `\n` terminated, ≤ 48 chars):

| Line | Meaning |
|---|---|
| `D,<lf>,<lb>,<rf>,<rb>,<k>,<d>` | drive: 4 wheel PWMs (−255..255), kicker request bit `k` ∈ {0,1}, dribbler bit `d` ∈ {0,1} — resets the watchdog |
| `STOP` | same effect as `T_STOP` above |

Fields are plain integers (`strtol`); a malformed line (bad prefix, missing fields, out-of-range PWM) is dropped whole — no partial application.

Interim Arduino→Pi:

| Line | Rate | Meaning |
|---|---|---|
| `H,<heading_cdeg>,<still>` | ~100 Hz | heading in centidegrees (−18000..18000), still flag 0/1 (drift-freeze) |
| `L,<v0>,<v1>,...,<v15>` | 25 Hz (interim; 50 Hz in the binary spec) | 16 raw ADC values 0..1023 |
| `C,<catcher>` | 10 Hz | catcher closed = 1 |
| `HB` | 2 Hz | heartbeat |

Interim bandwidth ≈ 2.9 kB/s Mega→Pi (25 %). This grammar is no longer compiled; `link_codec.*` and `link_rx.*`/`link_tx.*` now implement §3–§4 only.

## 8. Approval record

Approved in full by the user on **2026-11-18**; all eight checklist items were accepted as-is:

1. Framing: fixed binary + CRC8 (Option B).
2. `T_DRIVE` up to 100 Hz with kicker+dribbler bits; explicit `T_STOP`.
3. Telemetry split `T_HDG` @100 Hz + `T_LINE` @50 Hz + `T_HB` @2 Hz.
4. Watchdog 300 ms: motors, kicker and dribbler all stop; telemetry continues.
5. Heading as int16 centidegrees in the DMP frame, drift-freeze included.
6. Raw line values as uint16 @50 Hz; thresholds/angle computed on the Pi.
7. Catcher bit riding on `T_LINE`; no separate catcher frame.
8. Future extensions only as new TYPEs; existing frames never change.

Implementation status (2026-11-18): framing implemented behind the named seams on both sides; PC conformance suites green (`mega_fw` 21 checks, `robot_core` 216 checks, 0 failures); `mega_fw` PlatformIO compile green (RAM 18.8 %, flash 9.1 %). Next: bench with the robot lifted, then live distance/floor calibration with the user.
