# FaultWatch

Non-blocking LED fault indicator library for embedded systems.  
Drives single-color or two-color LEDs to show which fault bits are active in an error register — no `delay()`, no dynamic allocation, no `String` class.

Supports **AVR** (Uno / Nano / Mega), **ESP32**, **ESP8266**, **STM32**, and **RP2040**.

---

## Features

- **Single-color LED** — 5 blink patterns mapped to 8 error bits (single flash, dual blink, fast flash, slow blink, solid)
- **Two-color LED** — Dell-style diagnostic blink codes: color A blinks N times, gap, color B blinks M times, long pause, repeat
- **8 / 16 / 32-bit error register** — set via `FW_ERROR_BITS` compiler flag; scales all per-bit arrays automatically
- **Priority system** — priority 1 = exclusive lock (preempts all others); priorities 2–8 = round-robin cycling
- **`FW_DEBUG` macro** — compile-time gate on all internal `Serial.print` calls; zero overhead when off
- **RAM-optimized** — nibble-packed priority and code arrays, `uint8_t`-typed pins/counters, flag bits packed into one byte
- **Serial test interface** — full command set for runtime control without reflashing
- **Python automation test** — `pyserial`-based script that exercises every API function and prints a pass/fail checklist

---

## Supported boards

| Board | Architecture | RAM | Notes |
|---|---|---|---|
| Arduino Uno / Nano | AVR ATmega328P | 2 KB | Default target, 8-bit register |
| Arduino Mega | AVR ATmega2560 | 8 KB | 8 or 16-bit register |
| ESP32 | Xtensa LX6 | 520 KB | 16 or 32-bit register |
| ESP8266 | Xtensa L106 | 80 KB | 16-bit register recommended |
| STM32 (any) | ARM Cortex-M | varies | 32-bit register |
| RP2040 (Pico) | ARM Cortex-M0+ | 264 KB | 32-bit register |

---

## Installation

**Arduino IDE (manual):**
1. Download or clone this repository
2. Copy the `FaultWatch` folder into your Arduino `libraries/` directory
3. Restart the IDE

**PlatformIO:**
```ini
lib_deps = https://github.com/Dhaivatjoshi/FaultIndicator
```

---

## Wiring

```
Single-color:
  Pin 3 ──[220Ω]──[LED]── GND

Two-color:
  Pin 5 ──[220Ω]──[LED-A]── GND   (color A — blink group A)
  Pin 6 ──[220Ω]──[LED-B]── GND   (color B — blink group B)
```

---

## Quick start

```cpp
#include <FaultWatch.h>

FaultWatch singleLED(3);       // single-color on pin 3
FaultWatch twoColorLED(5, 6);  // two-color: pin 5 = A, pin 6 = B

void setup() {
    singleLED.setErrorByte(0b00000101);   // bits 0 and 2 active
    twoColorLED.setErrorByte(0b00000010); // bit 1 active
}

void loop() {
    singleLED.update();
    twoColorLED.update();
}
```

---

## Configuration

### Error register width

Set `FW_ERROR_BITS` as a **compiler flag** so both the library and your sketch see the same value.

| Value | Type | Bits | Platforms |
|---|---|---|---|
| `8` (default) | `uint8_t` | 0–7 | AVR, all others |
| `16` | `uint16_t` | 0–15 | ESP32, STM32 |
| `32` | `uint32_t` | 0–31 | ESP32, STM32, RP2040 |

**Arduino IDE** — add to `platform.local.txt`:
```
compiler.cpp.extra_flags=-DFW_ERROR_BITS=16
```

**PlatformIO** — add to `platformio.ini`:
```ini
build_flags = -DFW_ERROR_BITS=16
```

### Debug output

Define `FW_DEBUG 1` **before** the `#include` to enable internal serial tracing:

```cpp
#define FW_DEBUG 1
#include <FaultWatch.h>
```

When `FW_DEBUG 0` (default), all debug prints compile to nothing — zero flash and zero RAM overhead.

---

## API reference

### Constructor

```cpp
FaultWatch(int pin);              // single-color
FaultWatch(int pin1, int pin2);   // two-color (pin1=A, pin2=B)
```

### Error register

```cpp
void         setErrorByte(FwErrorType bits);  // set active fault bits
FwErrorType getErrorHex() const;              // read back masked register
```

### Priority

```cpp
void    setPriority(uint8_t bit, uint8_t priority);  // 1=exclusive, 2-8=round-robin
uint8_t getPriority(uint8_t bit) const;
```

Default: all bits = priority 2 (no exclusive, pure round-robin).

Priority 1 = when that bit is active it preempts every other error until cleared.

### Two-color diagnostic codes

```cpp
// Set blink counts for bit N: color A blinks colorACount times, then color B blinks colorBCount times
void setErrorCode(uint8_t bit, uint8_t colorACount, uint8_t colorBCount);  // counts 0-15

// Adjust timing (default: 300ms blink, 600ms gap between A and B groups)
void setCodeTiming(unsigned long blinkIntervalMs, unsigned long gapIntervalMs);

// Read back packed code as 0xAABB (e.g. code "2,1" → 0x0201)
uint16_t getDiagnosticCodeHex(uint8_t bit) const;
```

Default code for bit N: A=1, B=(N%8)+1.

### Pattern setters (single-color, manual use)

```cpp
void singleFlash(unsigned long interval);
void dualBlink(unsigned long interval);
void solid();
void fastFlash(unsigned long interval);
void slowDualBlink(unsigned long interval);
```

### Main loop

```cpp
void update();  // call every loop(), non-blocking
```

---

## Single-color blink pattern table

| Bit | Pattern | Interval |
|---|---|---|
| 0 | Single flash | 500 ms |
| 1 | Dual blink | 150 ms |
| 2 | Fast flash | 50 ms |
| 3 | Slow dual blink | 2000 ms |
| 4 | Fast flash | 50 ms |
| 5 | Fast flash | 100 ms |
| 6 | Single flash | 1000 ms |
| 7 | Dual blink | 250 ms |

For 16/32-bit registers, bit index wraps modulo 8.

---

## Two-color diagnostic code example

```
Bit 2 with code (3, 1):

  [A A A] ── gap ── [B] ── long pause ── repeat
   3×pin5           1×pin6
```

```cpp
twoColorLED.setErrorCode(2, 3, 1);   // 3 amber blinks, 1 red blink
twoColorLED.setCodeTiming(200, 500); // 200ms per blink, 500ms A-to-B gap
twoColorLED.setErrorByte(0b00000100);
```

---

## Serial test interface

Upload `examples/LedErrorIndicatorFullTest/LedErrorIndicatorFullTest.ino`, open Serial Monitor at **115200 baud**.

| Command | Description | Example |
|---|---|---|
| `s<0-255>` | Set SINGLE error byte | `s5` |
| `t<0-255>` | Set DOUBLE error byte | `t128` |
| `p<bit>,<1-8>` | Set priority for both LEDs | `p0,1` |
| `c<bit>,<A>,<B>` | Set TWO_COLOR blink code (A,B: 0-15) | `c0,2,1` |
| `g<blink>,<gap>` | Set code timing in ms | `g300,600` |
| `e` | Print both error registers as hex | `e` |
| `d<bit>` | Print TWO_COLOR diagnostic code hex | `d0` |
| `h` | Help menu | `h` |

---

## Python automation test

Requires `pyserial`:
```
pip install pyserial
```

Run against a connected Arduino:
```
python test/testLedErrorIndicatorFullTest.py COM3 115200
```

Output is printed to the console and appended to `test/testLedErrorIndicatorFullTest.log` with a timestamped header per run.

The script tests all 17 library functions (39 assertions) and prints a function-level checklist:
```
====================================================
                 FUNCTION CHECKLIST
====================================================
  Function                                   Status
----------------------------------------------------
  [OK] printHelp()                               1/1
  [OK] setErrorByte() SINGLE                     3/3
  ...
====================================================
  Functions : 17 OK, 0 failed
  Assertions: 39 passed, 0 failed
====================================================
```

Exit code `0` = all pass, `1` = any failure.

---

## Instance sizing (RAM per object)

| FW_ERROR_BITS | sizeof(FaultWatch) |
|---|---|
| 8 (default) | 40 bytes |
| 16 | 52 bytes |
| 32 | 80 bytes |

---

## License

MIT — see [LICENSE](LICENSE)

---

## Changelog

### v1.0.0 — 2026-06-16

**Initial release — Phase 0 complete.**

- Single-color LED support with 5 blink patterns mapped to 8 error bits via lookup table
- Two-color Dell-style diagnostic blink codes (`setErrorCode`, `setCodeTiming`, `getDiagnosticCodeHex`)
- 8/16/32-bit configurable error register via `FW_ERROR_BITS` compiler flag (`uint8_t` / `uint16_t` / `uint32_t`)
- Priority system: priority 1 = exclusive lock, 2–8 = round-robin; default all bits = 2
- `FW_DEBUG` compile-time macro — zero overhead when disabled
- RAM optimizations: nibble-packed priority and code arrays, `uint8_t`-typed pins/counters/indices, flags byte replaces 3 bools, `uint16_t` intervals, pattern lookup table replaces switch statements
- `getErrorHex()` — returns masked register with per-instance SINGLE/DOUBLE debug label
- `printHexPadded()` — zero-allocation hex printer in example sketch (no `String`, no `snprintf`)
- Full serial command test sketch (`LedErrorIndicatorFullTest.ino`) — no `delay()`, no auto-demo, all changes via serial
- Python automation test (`testLedErrorIndicatorFullTest.py`) — 17 functions, 39 assertions, append-mode log with run timestamps
- RGB / `analogWrite` removed — `digitalWrite` only
- No `delay()`, no dynamic allocation, no `String` class anywhere in library code
- Verified on AVR (Uno) with all 39 automated assertions passing
