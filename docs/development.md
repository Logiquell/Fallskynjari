# Development log

How the prototype was brought up, which decisions were made and why, and what
has actually been tested. Test results are **bench observations**: visual
readings of the OLED and a stopwatch, not instrumented measurements, unless
stated otherwise.

## Development approach

This project was developed with **Claude Code** (Anthropic) as an AI
development assistant. The split of work:

| Claude Code | Author |
|---|---|
| Wrote most of the firmware source (`blink/`, `oled/`, `logger/`) | Set the design requirements and chose the components |
| Looked up register and pin details in the XC8 headers and Microchip device files | Assembled and wired the breadboard |
| Ran the command-line build (`xc8-cc`) and programming (`ipecmd`) | Found the physical faults: loose MCLR wire, SDA on the wrong pin |
| Proposed fixes from the symptoms the author reported | Ran every bench test and reported the results: display readings, stopwatch timing, drop test |
| Drafted this documentation | Decided which feature to build next; reviewed the code and documentation |

Development went in small steps. Each change was compiled, flashed, verified
by the programmer and then checked on the hardware by the author before the
next part was added. Claude Code had no direct view of the hardware: every
result in the **Tests performed** section is what the author observed. Nothing
was simulated or assumed. **[confirm wording]**

## Timeline

| Date | Stage | Result |
|---|---|---|
| 2026-11-20 | Toolchain check: XC8 v3.00, MPLAB X v6.20, PICkit 5 detected over USB | OK |
| 2026-11-20 | `blink/`: LED on RC0 | Programmed and verified; LED blinks |
| 2026-11-20 | `oled/`: SSD1306 OLED on hardware I²C | Failed at first (see Debugging 1 and 2), then worked |
| 2026-11-20 | AHT20 added to the bus | Plausible temperature and humidity on the display |
| 2026-11-20 | ADXL345 added | Z = 0.99 g lying flat |
| 2026-11-20 | 24AA1025 added, write/read self-test | "EEPROM: OK" on both 64 KB blocks |
| 2026-11-21 | Design requirements reviewed against the hardware | Plan for sleep and wake-up, 400 Hz, and the log format |
| 2026-11-21 | `logger/`: low-power firmware, ADXL345 INT1 wired to RA2 | Boot error 5 once (see Debugging 4), then running |
| 2026-11-21 | Oscillator calibration and timekeeping | 29,275 Hz measured; timing error fixed (see Debugging 5) |
| 2026-11-21 | First hand drop test | t = 0.232 s, h = 0.26 m (see Tests) |
| 2026-11-21 | Serial console and CSV export | Implemented and flashed, **not tested** |
| 2026-11-22 | Documentation | This file, README, hardware and architecture notes |

## Design decisions

| Decision | Alternatives | Reason |
|---|---|---|
| Watchdog as the sleep timer, calibrated in software | Timer1 with a 32 kHz crystal; external RTC | The PIC16LF1559 has no Timer1 oscillator (no `T1OSCEN` bit). Calibrating against HFINTOSC gives about ±2 % without extra parts |
| ADXL345 at 400 Hz | 100 Hz (lower current) | At 100 Hz the height error at 0.2 m is about ±10 %; at 400 Hz about ±2.5 % |
| Hardware free-fall detection + FIFO stream mode | PIC polls the accelerometer continuously | The PIC can sleep; the FIFO keeps the 80 ms before the interrupt, so the start of the fall isn't lost |
| Height from free-fall time | Double-integrating acceleration | Free-fall time is easy to measure and insensitive to sensor offset. Integration drifts and needs orientation |
| No regulator, run straight from 2 × AA | LDO or buck converter | Every part except the OLED module runs down to about 2.0–2.2 V, and a regulator adds quiescent current |
| 100 kHz I²C | 400 kHz | The 24AA1025 only allows 400 kHz above 2.5 V; 100 kHz is fast enough for 400 Hz sampling |
| 16-byte log records, 8 per EEPROM page | Smaller packed records | No write crosses a page; room for a checksum and later fields; still 17 days of capacity |
| OLED off by default | Always on | The display uses far more current than everything else together (estimate; see hardware.md) |
| No `printf`; custom integer formatting | `printf` / `sprintf` | Program memory is limited (92.4 % used) |

## Debugging

### 1. Programmer: "Target Device ID (0x0) is an Invalid Device ID"

- **Symptom:** after the OLED was added, programming failed three times in a
  row. The PICkit powered the board (3.00 V) but read device ID 0x0.
- **Reasoning:** programming had worked before the OLED was added, so the new
  wiring was suspected. Candidates were a module wire on the ICSP pins 18/19, a
  bumped ICSP wire, or a short.
- **Cause:** the **MCLR** wire had come loose.
- **Same symptom later:** it happened again after the PICkit was reconnected.
  Re-checking the ICSP wiring fixed it; the specific wire wasn't recorded.

### 2. OLED dark and firmware hung

- **Symptom:** after a successful flash, neither the OLED nor the LED did
  anything.
- **Reasoning:** the LED blink was in the main loop after OLED setup. So the
  firmware was stuck during I²C, waiting forever for `SSP1IF`, which can happen
  with a missing pull-up or a wrong pin.
- **Checks:**
  1. Read the pin functions from the device pack's `.PIC` file: SDA is on RB4
     (pin 13) or RA3, chosen by `APFCON.SDSEL`, which resets to 0 = RB4.
  2. Added a diagnostic build: I²C timeouts, a bus-idle check and LED error
     codes 1–4.
- **Cause:** the SDA wire was on **RB5** (pin 12) instead of RB4 (pin 13).
- **Result:** the diagnostic features stayed in the firmware, so a bus fault
  can't hang it anymore.

### 3. XC8 error 1089: recursive function call

- **Symptom:** the build failed after an on-screen error report was added.
- **Cause:** `fail()` called `show_fail()`. That uses the I²C functions, which
  call `fail()` on a timeout. XC8 uses a compiled stack for this device and
  doesn't allow recursion.
- **Fix:** `show_fail()` is now called from `main` before `fail()`.

### 4. Boot error 5 (ADXL345 not found)

- **Symptom:** after INT1 was wired to pin 17, the board flashed error code 5
  and the display stayed dark, because the error happens before anything is
  drawn.
- **Response:** the boot error is now also shown on the OLED, with an I²C bus
  scan and the ADXL345 `DEVID` value.
- **Outcome:** on the next restart the device booted normally. The root cause
  was **not confirmed**. A loose contact on the ADXL345 module (for example CS,
  which sits next to INT1) is the most likely explanation.

### 5. Clock running slow

- **First finding:** LFINTOSC measured 29,275 Hz, 5.6 % below the 31 kHz
  nominal. Without calibration, the 180 s interval would have been about 190 s.
- **Symptom after calibration:** the uptime counter fell **about 3 s behind** a
  stopwatch over 3 minutes.
- **Cause:** only sleep periods were counted. With the display always on, each
  1-second tick included an uncounted display update (about 15–20 ms, an
  estimate), which is about 1.5 % of each tick.
- **Fix:** Timer1 counts LFINTOSC/8 whenever the CPU is awake. The fixed
  estimates for awake time were removed.
- **Result:** **under 1 s difference** from the stopwatch over 3 minutes.

### 6. PICkit 5 dropping off USB

The PICkit 5 disappeared from USB twice ("Programmer not found"). Reconnecting
it fixed it both times. The cause is not known (cable, port or hub); it's a
host-side issue, not a firmware one.

## Tests performed

| Test | Method | Result | Status |
|---|---|---|---|
| Programming and verify | `ipecmd -M -Y` | Verify succeeded on every flash | ✅ |
| I²C devices present | Boot probe and EEPROM self-test | AHT20, OLED, ADXL345 and 24AA1025 answer | ✅ |
| EEPROM integrity | Write a pattern and its inverse, 16 bytes in each 64 KB block, read back | Match | ✅ |
| Accelerometer at rest | Board flat, read Z | 0.99 g (expected 1.00 g; difference within ADXL345 offset tolerance plus integer truncation) | ✅ |
| Temperature / humidity | Visual check of the readings | Plausible values | ⚠️ no reference instrument |
| Oscillator calibration | Boot screen | LFINTOSC 29,275 Hz, tick 1119 ms (consistent: 32768 / 29275 = 1.119 s) | ✅ |
| Timekeeping | Uptime vs. stopwatch, 3 min | Under 1 s error | ✅ (single run) |
| Fall detection | One hand drop on the breadboard, about 25–30 cm | t = 0.232 s, h = 0.26 m, peak = 17.80 g, n = 93 | ⚠️ single, uncontrolled drop |

The drop-test numbers are internally consistent: 93 × 2.5 ms = 0.2325 s, and
½ × 9.81 × 0.232² = 0.264 m. The 17.80 g peak is above the ±16 g per-axis range,
so at least one axis was clipped. The peak is a lower bound; the height is not
affected.

## Not tested yet

- Serial console and CSV export (CP2102 not wired yet).
- Operation from 2 × AA cells, and `DISPLAY_ALWAYS_ON = 0`.
- Sleep current and average current.
- Drop tests at controlled heights: 0.2, 1 and 3 m, several repeats each.
- Falls with rotation, and false triggers from handling.
- Temperature and humidity against a reference instrument.
- Logging over 48 hours or longer, including log contents and timestamps.
- Behaviour at low battery voltage (OLED, brown-out).

## Known limitations

- The impact peak clips at ±16 g per axis.
- Timestamps are relative to power-up (no RTC).
- A fall that starts while the console is open is detected by polling, not by
  interrupt.
- Timekeeping depends on HFINTOSC accuracy (about ±2 % per datasheet); a single
  3-minute test showed under 0.6 %.
- Program memory is 92.4 % full, which limits new features without
  optimization.
- `DISPLAY_ALWAYS_ON` is currently **1** (bench setting).
