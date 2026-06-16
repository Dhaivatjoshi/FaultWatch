# FaultIndicator Library — Full Build Instruction

> Work through this document top to bottom with Claude Code + VS Code.
> Each section tells you exactly what to build, what files to create, and what to ask Claude Code to do.

## Progress tracker

| Phase | Title | Status |
|-------|-------|--------|
| 0 | Fix existing FaultWatch library | ✅ COMPLETED |
| — | Create FaultIndicator folder structure | ⬜ NEXT |
| 1 | `fi_config.h` | ⬜ |
| 2 | `fi_types.h` | ⬜ |
| 3 | `fi_hal.h` + `fi_hal_arduino.cpp` | ⬜ |
| 4 | `fault_indicator.h` (public API) | ⬜ |
| 5 | `fault_indicator.c` (core logic) | ⬜ |
| 6 | Output drivers (Serial, MQTT) | ⬜ |
| 7 | Arduino example (`arduino_basic.ino`) | ⬜ |
| 8 | Host-side unit tests (`test_core.cpp`) | ⬜ |
| 9 | ESP32 HAL (`fi_hal_esp32.cpp`) | ⬜ |
| 10 | STM32 HAL (`fi_hal_stm32_hal.c`) | ⬜ |

---

## What you are building

A universal embedded fault indicator middleware library called **FaultIndicator** (`fi_`).

- Multiple LED channels run in parallel — each owns one domain (power, sensor, comms, system)
- Each channel has an `fi_word_t` error word (8/16/32 bits — configurable)
- LED shows severity (off / slow blink / fast blink / SOS) based on worst active error
- Output callbacks fire on change — Serial, LCD, MQTT, BLE all get the same event struct
- HAL layer isolates platform — one small file per MCU, core never changes
- Targets: Arduino AVR → ESP8266/ESP32 → STM32 → RP2040 → PIC (in order)

---

## Folder structure to create first

Ask Claude Code to create this exact structure:

```
FaultIndicator/
├── src/
│   ├── fault_indicator.h        ← public API (user includes this)
│   ├── fault_indicator.c        ← core logic (never platform-specific)
│   ├── fi_config.h              ← user config (word size, max channels)
│   ├── fi_types.h               ← all typedefs and enums
│   ├── fi_pattern.h             ← pattern engine header
│   ├── fi_pattern.c             ← pattern engine (blink logic)
│   ├── hal/
│   │   ├── fi_hal.h             ← HAL interface (4 functions to implement)
│   │   ├── fi_hal_arduino.cpp   ← Arduino HAL (V1)
│   │   ├── fi_hal_esp32.cpp     ← ESP32 HAL (V2)
│   │   ├── fi_hal_stm32_hal.c   ← STM32 HAL layer (V3)
│   │   ├── fi_hal_stm32_bare.c  ← STM32 bare-metal (V3b)
│   │   └── fi_hal_rp2040.c      ← RP2040 (V4)
│   └── outputs/
│       ├── fi_output.h          ← output callback interface
│       ├── fi_output_serial.cpp ← Serial output driver
│       ├── fi_output_lcd.cpp    ← LCD output driver (I2C LCD)
│       └── fi_output_mqtt.cpp   ← MQTT output driver (ESP32)
├── examples/
│   ├── arduino_basic/
│   │   └── arduino_basic.ino    ← 3-channel example, Uno/Nano
│   ├── esp32_mqtt/
│   │   └── esp32_mqtt.ino       ← WiFi + MQTT output example
│   └── stm32_uart/
│       └── main.c               ← STM32 bare-metal example
├── tests/
│   └── test_core.cpp            ← unit tests (host PC, no hardware needed)
├── library.properties           ← Arduino IDE metadata
├── README.md
└── INSTRUCTION.md               ← this file
```

**Prompt for Claude Code:**
```
Create the folder structure for FaultIndicator library exactly as shown
in the INSTRUCTION.md. Create all files as empty placeholders with a
single comment line showing the file purpose. Do not write any logic yet.
```

---

## Phase 0 — Fix the existing library first ✅ COMPLETED

Before writing new code, fix the bugs in the old `FaultWatch` library.
This is important so you understand what you are replacing.

### Bug list — all 5 fixed

| # | File | Bug | Fix | Status |
|---|------|-----|-----|--------|
| 1 | FaultWatch.cpp | `Serial.println()` left in `setErrorByte()` | Replaced with `LED_DEBUG_PRINTLN` macro | ✅ |
| 2 | FaultWatch.cpp | `_firsterror` never resets between errors | `_firsterror = false` added on pattern transition and next-error advance | ✅ |
| 3 | FaultWatch.cpp | `static unsigned int previndex` declared but never used | Deleted | ✅ |
| 4 | FaultWatch.cpp | Double `for` loop to find next error bit | Replaced with single modulo scan `nextActiveBit()` | ✅ |
| 5 | FaultWatch.cpp | `_currentErrorIndex++` has no bounds check | Replaced by `nextActiveBit()` which wraps via `% LED_ERROR_BITS` | ✅ |

### Additions made beyond the original bug list

These features were added during Phase 0 to bring the library to a useful state
before the full FaultIndicator rewrite.

| Feature | Files changed | Notes |
|---------|---------------|-------|
| **RGB support removed** | `.cpp`, `.h` | `analogWrite` removed entirely. Only `digitalWrite` used. |
| **`LED_DEBUG` macro** | `.h`, `.cpp` | `#define LED_DEBUG 0/1` before `#include`. Zero overhead when off. `LED_DEBUG_PRINT` / `LED_DEBUG_PRINTLN` macros gate all internal prints. |
| **Priority system** | `.h`, `.cpp` | `setPriority(bit, 1..8)`. Priority 1 = exclusive lock (only that error shown until cleared). Priorities 2–8 = round-robin. Default: all bits = priority 2 (no exclusive by default). |
| **TWO_COLOR Dell-style diagnostic codes** | `.h`, `.cpp` | `setErrorCode(bit, A, B)` — color A blinks A times, pause, color B blinks B times, pause, repeat. `setCodeTiming(blinkMs, gapMs)` adjusts timing. Default: bit N → `(1, (N%8)+1)`. |
| **`LED_ERROR_BITS` configurable register** | `.h`, `.cpp` | `#define LED_ERROR_BITS 8/16/32` scales `_errorByte` type (`uint8_t`/`uint16_t`/`uint32_t`) and all per-bit arrays. `LED_ERROR_MASK` and `LED_ERROR_HEX_DIGITS` macros generated automatically. **Must be defined consistently across all TUs (compiler flag or shared config header).** |
| **`getErrorHex()`** | `.h`, `.cpp` | Returns `_errorByte & LED_ERROR_MASK`. Debug print auto-labels SINGLE or DOUBLE based on `_ledType`. No `String`, no allocation. |
| **`getDiagnosticCodeHex(bit)`** | `.h`, `.cpp` | Returns `0xAABB` packed from `_codeA[bit]`/`_codeB[bit]`. TWO_COLOR only. |
| **`printHexPadded()` helper** | example `.ino` | Nibble-loop printer (no `String`/`snprintf`). Pads to `LED_ERROR_HEX_DIGITS` digits for error register; always 4 digits for diagnostic code. |
| **Updated example sketch** | `LedErrorIndicatorFullTest.ino` | Serial commands: `s`, `t` (set error bytes), `p` (priority), `c` (diagnostic code), `g` (timing), `e` (read error hex), `d<bit>` (read diagnostic code hex). No auto-demo — all changes via serial only. |

### Phase 0 completion checklist

- [x] All 5 original bugs fixed in `FaultWatch.cpp`
- [x] `LED_DEBUG` macro compiles to zero overhead when `LED_DEBUG 0`
- [x] RGB / `analogWrite` removed — only `digitalWrite` used
- [x] Priority system: exclusive (1) and round-robin (2–8) verified with time-stepped g++ test
- [x] TWO_COLOR blink-code engine verified with simulated `millis()` stepping
- [x] `LED_ERROR_BITS` compiles and runs correctly for 8, 16, and 32-bit configurations
- [x] `getErrorHex()` and `getDiagnosticCodeHex()` return correctly masked, properly typed values
- [x] Example sketch exercises all features via serial commands — no `delay()`, no auto-demo
- [x] All files compile with `-Wall` and zero warnings

**Prompt for Claude Code:**
```
In FaultWatch.cpp apply these 5 bug fixes exactly as described
in INSTRUCTION.md Phase 0. Show me a diff of every change made.
```

---

## Phase 1 — fi_config.h (start here, most important file)

This file controls everything. Write it first. All other files depend on it.

### What it must contain

```c
#ifndef FI_CONFIG_H
#define FI_CONFIG_H

// ── Error word size ─────────────────────────────────────────
// uint8_t  = 8 bits per channel  (best for AVR: Uno, Nano, Mega, PIC)
// uint16_t = 16 bits per channel (good for ESP8266, medium projects)
// uint32_t = 32 bits per channel (best for STM32, ESP32, RP2040)
#ifndef FI_WORD_TYPE
  #define FI_WORD_TYPE uint8_t
#endif

// ── Max channels ────────────────────────────────────────────
// How many LED channels can the manager hold
// Each channel uses ~(12 + FI_MAX_BITS * 4) bytes RAM
// Default 4 channels costs ~52 bytes RAM with uint8_t
#ifndef FI_MAX_CHANNELS
  #define FI_MAX_CHANNELS 4
#endif

// ── Max output callbacks ─────────────────────────────────────
// How many output handlers (Serial, LCD, MQTT...) can be registered
#ifndef FI_MAX_OUTPUTS
  #define FI_MAX_OUTPUTS 3
#endif

// ── Timing source ───────────────────────────────────────────
// FI_TICK_MS  = millisecond ticks (default — works everywhere)
// FI_TICK_US  = microsecond ticks (future — high speed sampling)
#define FI_TICK_MS

// ── Platform selection ──────────────────────────────────────
// Uncomment ONE that matches your board.
// Or pass -DFI_PLATFORM_ARDUINO from your build system.
// #define FI_PLATFORM_ARDUINO      // Uno, Nano, Mega, Due, Zero
// #define FI_PLATFORM_ESP32        // ESP32, ESP32-S2/S3/C3
// #define FI_PLATFORM_ESP8266      // NodeMCU, Wemos D1
// #define FI_PLATFORM_STM32_HAL    // STM32 with CubeIDE HAL
// #define FI_PLATFORM_STM32_BARE   // STM32 direct registers
// #define FI_PLATFORM_RP2040       // Raspberry Pi Pico

#endif // FI_CONFIG_H
```

**Prompt for Claude Code:**
```
Write fi_config.h for the FaultIndicator library exactly as shown in
INSTRUCTION.md Phase 1. Add a compile-time check using #if that prints
a clear error message if FI_MAX_BITS would exceed 32.
```

---

## Phase 2 — fi_types.h (all types in one place)

### What it must contain

```c
// ── Derived from config ──────────────────────────────────────
typedef FI_WORD_TYPE fi_word_t;
#define FI_MAX_BITS   ((uint8_t)(sizeof(fi_word_t) * 8))

// ── Severity levels ──────────────────────────────────────────
typedef enum {
    FI_SEV_NONE     = 0,   // bit slot unused
    FI_SEV_INFO     = 1,   // informational — LED slow pulse 0.5Hz
    FI_SEV_WARNING  = 2,   // warning        — LED single flash 1Hz
    FI_SEV_ERROR    = 3,   // error          — LED fast flash 4Hz
    FI_SEV_CRITICAL = 4    // critical       — LED rapid SOS
} FI_Severity_t;

// ── LED types ────────────────────────────────────────────────
typedef enum {
    FI_LED_NONE       = 0,  // no LED (channel is output-only)
    FI_LED_SINGLE     = 1,  // one pin — on/off
    FI_LED_TWO_COLOR  = 2,  // two pins — color A or color B
    FI_LED_RGB        = 3   // three pins — full color
} FI_LedType_t;

// ── LED configuration ────────────────────────────────────────
typedef struct {
    FI_LedType_t type;
    uint8_t      pin_a;    // single: LED pin | two-color: color A | RGB: R
    uint8_t      pin_b;    // two-color: color B | RGB: G
    uint8_t      pin_c;    // RGB: B
    uint8_t      active_high; // 1=HIGH turns on, 0=LOW turns on
} FI_LedConfig_t;

// ── Per-bit metadata ─────────────────────────────────────────
typedef struct {
    const char   *name;      // "low_voltage", "temp_fail" etc
    FI_Severity_t severity;  // how bad is this error
} FI_BitMeta_t;

// ── Event — what output callbacks receive ────────────────────
typedef struct {
    const char   *channel_name;  // "POWER", "SENSOR"
    uint8_t       bit;           // which bit changed (0 to FI_MAX_BITS-1)
    const char   *bit_name;      // "overvoltage", "temp_fail"
    FI_Severity_t severity;      // severity of the changed bit
    uint8_t       is_set;        // 1 = error set, 0 = error cleared
    fi_word_t     error_word;    // full current state of channel
    uint32_t      timestamp;     // fi_get_ticks() when it happened
} FI_Event_t;

// ── Output callback type ─────────────────────────────────────
typedef void (*FI_OutputCb_t)(FI_Event_t *event);

// ── Channel ──────────────────────────────────────────────────
typedef struct {
    char          name[12];              // channel name
    FI_LedConfig_t led;                  // LED hardware config
    fi_word_t     error_word;            // current active errors
    fi_word_t     prev_word;             // last known state (change detection)
    FI_BitMeta_t  bits[FI_MAX_BITS];     // metadata per bit
    // pattern state (internal — do not touch directly)
    uint8_t       _pattern;              // current pattern ID
    uint8_t       _flash_count;          // flashes done so far
    uint8_t       _led_state;            // current LED on/off
    uint32_t      _prev_millis;          // last toggle time
} FI_Channel_t;

// ── Manager ──────────────────────────────────────────────────
typedef struct {
    FI_Channel_t  *channels[FI_MAX_CHANNELS];
    uint8_t        channel_count;
    FI_OutputCb_t  outputs[FI_MAX_OUTPUTS];
    uint8_t        output_count;
} FI_Manager_t;
```

**Prompt for Claude Code:**
```
Write fi_types.h for FaultIndicator using the struct and enum definitions
from INSTRUCTION.md Phase 2. Include fi_config.h at the top.
Add a static assert that FI_MAX_BITS is between 8 and 32 inclusive.
```

---

## Phase 3 — fi_hal.h + fi_hal_arduino.cpp (HAL layer)

### fi_hal.h — the interface every platform must implement

```c
// These 4 functions are the only platform-specific things.
// Implement them in fi_hal_arduino.cpp, fi_hal_stm32.c etc.

void     fi_pin_mode  (uint8_t pin, uint8_t mode);   // INPUT or OUTPUT
void     fi_pin_write (uint8_t pin, uint8_t state);  // HIGH or LOW
void     fi_pwm_write (uint8_t pin, uint8_t value);  // 0-255 PWM
uint32_t fi_get_ticks (void);                        // milliseconds
```

### fi_hal_arduino.cpp — V1 implementation

```cpp
#include "fi_hal.h"
#include <Arduino.h>

void     fi_pin_mode  (uint8_t pin, uint8_t mode)  { pinMode(pin, mode);          }
void     fi_pin_write (uint8_t pin, uint8_t state) { digitalWrite(pin, state);    }
void     fi_pwm_write (uint8_t pin, uint8_t value) { analogWrite(pin, value);     }
uint32_t fi_get_ticks (void)                       { return (uint32_t)millis();   }
```

That is the entire Arduino HAL. 4 lines of real code.

**Prompt for Claude Code:**
```
Write fi_hal.h with exactly the 4 function declarations shown in
INSTRUCTION.md Phase 3. Then write fi_hal_arduino.cpp that implements
all 4 using Arduino functions. Add #ifdef FI_PLATFORM_ARDUINO guards
so it only compiles when that platform is selected.
```

---

## Phase 4 — fault_indicator.h (public API)

This is what the user `#include`s. It must be clean and simple.

### Functions to declare

```c
// ── Manager setup ────────────────────────────────────────────
void FI_manager_init    (FI_Manager_t *fm);
void FI_channel_add     (FI_Manager_t *fm, FI_Channel_t *ch);
void FI_output_add      (FI_Manager_t *fm, FI_OutputCb_t cb);

// ── Channel setup ────────────────────────────────────────────
void FI_channel_init    (FI_Channel_t *ch, const char *name, FI_LedConfig_t led);
void FI_bit_define      (FI_Channel_t *ch, uint8_t bit,
                         const char *name, FI_Severity_t severity);

// ── Setting errors — call from anywhere in your code ─────────
void FI_set             (FI_Channel_t *ch, uint8_t bit);
void FI_clear           (FI_Channel_t *ch, uint8_t bit);
void FI_set_word        (FI_Channel_t *ch, fi_word_t word);
void FI_clear_all       (FI_Channel_t *ch);

// ── Reading errors ───────────────────────────────────────────
uint8_t      FI_has_error  (FI_Channel_t *ch, uint8_t bit);
uint8_t      FI_any_error  (FI_Channel_t *ch);
fi_word_t    FI_get_word   (FI_Channel_t *ch);
FI_Severity_t FI_worst     (FI_Channel_t *ch);

// ── Main update — call every loop ────────────────────────────
void FI_manager_update  (FI_Manager_t *fm);
```

**Prompt for Claude Code:**
```
Write fault_indicator.h for the FaultIndicator library.
It must include fi_config.h and fi_types.h.
Declare all functions from INSTRUCTION.md Phase 4.
Add a C++ extern "C" guard so it works in both C and C++ projects.
Add include guards.
```

---

## Phase 5 — fault_indicator.c (core logic)

This is the main implementation file. It must never include Arduino.h, stm32xx.h, or any platform header. Only `fault_indicator.h` and `fi_hal.h`.

### Function implementation order — do them in this order

**Step 5.1 — FI_manager_init**
```c
void FI_manager_init(FI_Manager_t *fm) {
    fm->channel_count = 0;
    fm->output_count  = 0;
    // zero all pointers
    for (uint8_t i = 0; i < FI_MAX_CHANNELS; i++) fm->channels[i] = NULL;
    for (uint8_t i = 0; i < FI_MAX_OUTPUTS;  i++) fm->outputs[i]  = NULL;
}
```

**Step 5.2 — FI_channel_init**
```c
void FI_channel_init(FI_Channel_t *ch, const char *name, FI_LedConfig_t led) {
    // copy name (safe strncpy)
    // store led config
    // zero error_word and prev_word
    // zero all bit metadata
    // zero all pattern state fields
    // call fi_pin_mode to configure LED pins as OUTPUT
}
```

**Step 5.3 — FI_bit_define**
```c
void FI_bit_define(FI_Channel_t *ch, uint8_t bit, const char *name, FI_Severity_t sev) {
    if (bit >= FI_MAX_BITS) return;  // safety guard
    ch->bits[bit].name     = name;
    ch->bits[bit].severity = sev;
}
```

**Step 5.4 — FI_set and FI_clear**

These are the most important functions. After changing the error word, they must check if the word changed and fire output callbacks if so.

```c
void FI_set(FI_Channel_t *ch, uint8_t bit) {
    if (bit >= FI_MAX_BITS) return;
    ch->error_word |= (fi_word_t)1 << bit;
    // change detection happens in FI_manager_update — not here
    // set is instant — update drives LED and callbacks
}

void FI_clear(FI_Channel_t *ch, uint8_t bit) {
    if (bit >= FI_MAX_BITS) return;
    ch->error_word &= ~((fi_word_t)1 << bit);
}
```

**Step 5.5 — FI_worst (finds highest severity of all active bits)**
```c
FI_Severity_t FI_worst(FI_Channel_t *ch) {
    FI_Severity_t worst = FI_SEV_NONE;
    for (uint8_t i = 0; i < FI_MAX_BITS; i++) {
        if ((ch->error_word >> i) & 1) {
            if (ch->bits[i].severity > worst) {
                worst = ch->bits[i].severity;
            }
        }
    }
    return worst;
}
```

**Step 5.6 — Change detection and output firing**

This runs inside `FI_manager_update`. It compares current word to previous word, finds which bits changed, and fires callbacks.

```c
// Called per channel inside FI_manager_update
static void fi_check_changes(FI_Manager_t *fm, FI_Channel_t *ch) {
    fi_word_t changed = ch->error_word ^ ch->prev_word;
    if (changed == 0) return;  // nothing changed — exit fast

    for (uint8_t i = 0; i < FI_MAX_BITS; i++) {
        if ((changed >> i) & 1) {
            // this bit changed — build event and fire all outputs
            FI_Event_t ev;
            ev.channel_name = ch->name;
            ev.bit          = i;
            ev.bit_name     = ch->bits[i].name ? ch->bits[i].name : "unknown";
            ev.severity     = ch->bits[i].severity;
            ev.is_set       = (ch->error_word >> i) & 1;
            ev.error_word   = ch->error_word;
            ev.timestamp    = fi_get_ticks();

            for (uint8_t j = 0; j < fm->output_count; j++) {
                if (fm->outputs[j]) fm->outputs[j](&ev);
            }
        }
    }
    ch->prev_word = ch->error_word;  // update after firing all callbacks
}
```

**Step 5.7 — Pattern engine (LED blink logic)**

Severity maps to pattern. Pattern engine is non-blocking using fi_get_ticks().

```c
// Severity → blink interval mapping
// FI_SEV_NONE     → LED OFF
// FI_SEV_INFO     → slow pulse  2000ms period
// FI_SEV_WARNING  → single flash 500ms period
// FI_SEV_ERROR    → fast flash  200ms period
// FI_SEV_CRITICAL → rapid flash 100ms period

static void fi_update_led(FI_Channel_t *ch) {
    FI_Severity_t sev = FI_worst(ch);
    uint32_t now = fi_get_ticks();
    uint32_t interval;

    if (sev == FI_SEV_NONE) {
        // all clear — LED off
        fi_led_off(ch);
        return;
    }

    // map severity to interval
    switch (sev) {
        case FI_SEV_INFO:     interval = 1000; break;
        case FI_SEV_WARNING:  interval = 400;  break;
        case FI_SEV_ERROR:    interval = 150;  break;
        case FI_SEV_CRITICAL: interval = 80;   break;
        default:              interval = 500;  break;
    }

    if (now - ch->_prev_millis >= interval) {
        ch->_prev_millis = now;
        ch->_led_state   = !ch->_led_state;
        fi_led_write(ch, ch->_led_state);
    }
}
```

**Step 5.8 — FI_manager_update (the main loop function)**

```c
void FI_manager_update(FI_Manager_t *fm) {
    for (uint8_t i = 0; i < fm->channel_count; i++) {
        FI_Channel_t *ch = fm->channels[i];
        if (!ch) continue;
        fi_check_changes(fm, ch);  // fire callbacks if word changed
        fi_update_led(ch);         // drive LED based on worst severity
    }
}
```

**Prompt for Claude Code:**
```
Write fault_indicator.c implementing all 8 steps from INSTRUCTION.md
Phase 5 in order. Include only fault_indicator.h and fi_hal.h.
No Arduino.h or any platform header.
Add static keyword to all internal helper functions.
After writing, check every function — if it calls hardware directly
without going through fi_hal.h functions, that is a bug, fix it.
```

---

## Phase 6 — Output drivers

### fi_output_serial.cpp

```cpp
// Works on any platform with Serial support (Arduino, ESP32)
#include "fi_output.h"

void FI_output_serial(FI_Event_t *ev) {
    Serial.print("[");
    Serial.print(ev->channel_name);
    Serial.print("] ");
    Serial.print(ev->bit_name);
    Serial.print(ev->is_set ? " SET " : " CLR ");
    switch (ev->severity) {
        case FI_SEV_CRITICAL: Serial.print("CRITICAL"); break;
        case FI_SEV_ERROR:    Serial.print("ERROR");    break;
        case FI_SEV_WARNING:  Serial.print("WARNING");  break;
        case FI_SEV_INFO:     Serial.print("INFO");     break;
        default:              Serial.print("NONE");     break;
    }
    Serial.print(" word=0x");
    Serial.print((unsigned long)ev->error_word, HEX);
    Serial.print(" t=");
    Serial.println(ev->timestamp);
}
```

### fi_output_mqtt.cpp (ESP32 only)

```cpp
// Requires PubSubClient library
// User must pass in their PubSubClient instance
#include "fi_output.h"

static PubSubClient *_mqtt_client = NULL;
static char _mqtt_topic_buf[64];

void FI_output_mqtt_init(PubSubClient *client, const char *base_topic) {
    _mqtt_client = client;
    strncpy(_mqtt_topic_buf, base_topic, sizeof(_mqtt_topic_buf) - 1);
}

void FI_output_mqtt(FI_Event_t *ev) {
    if (!_mqtt_client || !_mqtt_client->connected()) return;

    char topic[80];
    char payload[128];

    snprintf(topic, sizeof(topic), "%s/%s", _mqtt_topic_buf, ev->channel_name);
    snprintf(payload, sizeof(payload),
        "{\"bit\":%d,\"name\":\"%s\",\"set\":%d,\"sev\":%d,\"word\":%lu,\"t\":%lu}",
        ev->bit, ev->bit_name, ev->is_set, ev->severity,
        (unsigned long)ev->error_word, (unsigned long)ev->timestamp);

    _mqtt_client->publish(topic, payload);
}
```

**Prompt for Claude Code:**
```
Write fi_output_serial.cpp and fi_output_mqtt.cpp as shown in
INSTRUCTION.md Phase 6. For MQTT, add a check: if _mqtt_client is NULL
print a warning to Serial instead of crashing. Add a comment at the top
of each file listing which platforms it works on.
```

---

## Phase 7 — Arduino example (first real test)

Create `examples/arduino_basic/arduino_basic.ino`:

```cpp
// FaultIndicator — Arduino basic example
// Tests 3 channels: power, sensor, comms
// Hardware: 3 LEDs on pins 9, 10, 11
//           Serial monitor at 115200 baud

#define FI_WORD_TYPE      uint8_t    // 8 bits per channel
#define FI_MAX_CHANNELS   3
#define FI_PLATFORM_ARDUINO

#include "fault_indicator.h"
#include "outputs/fi_output_serial.cpp"

// ── Manager and channels ─────────────────────────────────────
FI_Manager_t  fm;
FI_Channel_t  power_ch, sensor_ch, comms_ch;

// ── LED configs ──────────────────────────────────────────────
FI_LedConfig_t led_red   = {FI_LED_SINGLE, 9,  0, 0, 1};
FI_LedConfig_t led_green = {FI_LED_SINGLE, 10, 0, 0, 1};
FI_LedConfig_t led_blue  = {FI_LED_SINGLE, 11, 0, 0, 1};

void setup() {
    Serial.begin(115200);
    Serial.println("FaultIndicator V1 — Arduino basic example");

    // Init manager
    FI_manager_init(&fm);

    // Setup power channel
    FI_channel_init(&power_ch, "POWER", led_red);
    FI_bit_define(&power_ch, 0, "low_voltage",  FI_SEV_WARNING);
    FI_bit_define(&power_ch, 1, "overvoltage",  FI_SEV_CRITICAL);
    FI_bit_define(&power_ch, 2, "brownout",     FI_SEV_ERROR);
    FI_bit_define(&power_ch, 3, "battery_crit", FI_SEV_CRITICAL);

    // Setup sensor channel
    FI_channel_init(&sensor_ch, "SENSOR", led_green);
    FI_bit_define(&sensor_ch, 0, "temp_fail",   FI_SEV_ERROR);
    FI_bit_define(&sensor_ch, 1, "temp_range",  FI_SEV_WARNING);
    FI_bit_define(&sensor_ch, 2, "humid_fail",  FI_SEV_ERROR);
    FI_bit_define(&sensor_ch, 3, "adc_error",   FI_SEV_ERROR);

    // Setup comms channel
    FI_channel_init(&comms_ch, "COMMS", led_blue);
    FI_bit_define(&comms_ch, 0, "wifi_down",    FI_SEV_WARNING);
    FI_bit_define(&comms_ch, 1, "mqtt_disc",    FI_SEV_WARNING);
    FI_bit_define(&comms_ch, 2, "uart_ovf",     FI_SEV_ERROR);

    // Add channels to manager
    FI_channel_add(&fm, &power_ch);
    FI_channel_add(&fm, &sensor_ch);
    FI_channel_add(&fm, &comms_ch);

    // Register Serial output
    FI_output_add(&fm, FI_output_serial);

    Serial.println("Setup complete. Starting error simulation...");
}

void loop() {
    // ── Simulate errors changing over time ───────────────────
    static uint32_t last_sim = 0;
    static uint8_t  sim_step = 0;

    if (millis() - last_sim >= 3000) {
        last_sim = millis();
        sim_step++;

        switch (sim_step % 6) {
            case 0: FI_set  (&power_ch,  0); break;  // low voltage
            case 1: FI_set  (&sensor_ch, 0); break;  // temp fail
            case 2: FI_set  (&power_ch,  1); break;  // overvoltage (critical!)
            case 3: FI_clear(&power_ch,  0); break;  // low voltage cleared
            case 4: FI_clear(&power_ch,  1); break;  // overvoltage cleared
            case 5: FI_clear_all(&sensor_ch); break; // all sensor errors clear
        }
    }

    // ── This is the only call needed in loop ─────────────────
    FI_manager_update(&fm);
}
```

**Prompt for Claude Code:**
```
Write examples/arduino_basic/arduino_basic.ino as shown in
INSTRUCTION.md Phase 7. Then verify: does FI_manager_update() get
called unconditionally every loop with no delay() before or after it?
If any delay() was added, remove it — the library is non-blocking and
must never block the loop.
```

---

## Phase 8 — Host-side unit tests (no hardware needed)

Create `tests/test_core.cpp` — runs on PC, no MCU needed. Tests the core logic.

**Prompt for Claude Code:**
```
Write tests/test_core.cpp for FaultIndicator. It must:
1. Stub fi_hal functions (fi_pin_mode, fi_pin_write, fi_pwm_write, fi_get_ticks)
   with simple C functions — fi_get_ticks returns a global counter
2. Test FI_set sets the correct bit
3. Test FI_clear clears the correct bit
4. Test FI_worst returns the highest severity of all set bits
5. Test that output callback fires exactly once when a bit changes
6. Test that output callback does NOT fire when the word does not change
7. Test that FI_MAX_BITS guard prevents writing to out-of-range bits
Use assert() for all checks. Print PASS or FAIL per test.
Compile with: gcc tests/test_core.cpp src/fault_indicator.c -I src -o test_runner
```

---

## Phase 9 — ESP32 HAL (V2)

Create `src/hal/fi_hal_esp32.cpp`:

Key differences from Arduino HAL:
- `analogWrite()` does not exist on ESP32 — use `ledcWrite()` instead
- PWM requires `ledcSetup()` and `ledcAttachPin()` first
- `millis()` works the same

**Prompt for Claude Code:**
```
Write fi_hal_esp32.cpp for FaultIndicator.
The key difference: ESP32 does not have analogWrite().
Use LEDC peripheral instead:
  ledcSetup(channel, 5000, 8) for 5kHz 8-bit PWM
  ledcAttachPin(pin, channel) to connect pin
  ledcWrite(channel, value) to set duty cycle
Store a pin-to-channel mapping array internally.
Support up to 8 PWM pins (ESP32 has 16 LEDC channels).
Add #ifdef FI_PLATFORM_ESP32 guards.
```

---

## Phase 10 — STM32 HAL (V3)

Create `src/hal/fi_hal_stm32_hal.c`:

**Prompt for Claude Code:**
```
Write fi_hal_stm32_hal.c for FaultIndicator targeting STM32 with
STM32 HAL library (STM32CubeIDE / CubeMX generated project).
Use these STM32 HAL functions:
  HAL_GPIO_WritePin(GPIOx, GPIO_Pin, state) for pin write
  HAL_GetTick() for millisecond ticks
  TIM_HandleTypeDef + HAL_TIM_PWM_Start for PWM
For the pin abstraction: define a FI_STM32_PinMap_t struct that holds
{GPIO_TypeDef *port, uint16_t pin} and an array the user populates.
The fi_pin_write(pin, state) function indexes into that array.
Add #ifdef FI_PLATFORM_STM32_HAL guards.
Document clearly in comments how the user fills the pin map array.
```

---

## Version checklist

### V1 — Arduino ✓ complete when:
- [ ] `fi_config.h` compiles with `uint8_t`, `uint16_t`, `uint32_t`
- [ ] `fi_hal_arduino.cpp` compiles on Uno
- [ ] `arduino_basic.ino` uploads and LEDs blink correctly
- [ ] Serial output prints one line per error change (not every loop)
- [ ] `test_core.cpp` passes all 7 tests on PC
- [ ] No `delay()` anywhere in library code
- [ ] No `Serial.println()` inside library core (only in output drivers)

### V2 — ESP32 ✓ complete when:
- [ ] `fi_hal_esp32.cpp` compiles for ESP32 target
- [ ] RGB LED works via LEDC PWM
- [ ] `fi_output_mqtt.cpp` publishes correct JSON to broker
- [ ] WiFi reconnect does not block `FI_manager_update()`

### V3 — STM32 ✓ complete when:
- [ ] `fi_hal_stm32_hal.c` compiles in CubeIDE project
- [ ] Pin map array documented and working
- [ ] UART output driver added for STM32
- [ ] Tested on STM32F103 Blue Pill and STM32F401 Nucleo

---

## Rules to follow during all development

1. **Core files** (`fault_indicator.c`, `fi_types.h`, `fi_config.h`, `fi_pattern.c`) must never include any platform header. If Claude Code adds `#include <Arduino.h>` to these files — remove it immediately.

2. **No delay() anywhere** in library code. If timing is needed, use `fi_get_ticks()` subtraction pattern.

3. **No Serial.print() in core**. Only output driver files (`fi_output_serial.cpp` etc.) may touch Serial.

4. **Every function that modifies `error_word` must bounds-check the bit parameter** before doing anything.

5. **Change detection is the only time outputs fire**. Outputs must never be called every loop — only when `error_word != prev_word`.

6. **Test on PC first** using `test_core.cpp` before flashing to hardware. Most logic bugs are caught without any MCU.

7. **Commit after each Phase completes** with a message like `feat: Phase 3 HAL layer Arduino complete`.

---

## Useful prompts to copy-paste into Claude Code

```
# After writing any .c or .h file:
Review this file against INSTRUCTION.md rules:
1. Does it include any platform header? (Arduino.h, stm32xx.h etc)
2. Does it use delay() anywhere?
3. Does it use Serial.print() outside of output driver files?
4. Does every bit-manipulation function bounds-check the bit parameter?
List any violations found.
```

```
# When a test fails:
test_core.cpp test [NAME] is failing. The assertion is [PASTE ASSERT LINE].
Show me the execution path through fault_indicator.c that leads to this
assertion and explain why it fails. Then fix it.
```

```
# When starting a new HAL file:
I am starting fi_hal_[PLATFORM].c. Before writing any code, list every
function in fi_hal.h that I need to implement. Then implement each one
using only [PLATFORM] native APIs. Do not use any Arduino-specific
functions. Show me which native API call maps to each HAL function.
```

---

*Phase 0 complete — next: create FaultIndicator folder structure, then Phase 1 (fi_config.h)*
