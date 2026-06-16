// ─────────────────────────────────────────────────────────────
// FaultWatch  v1.0.0
// Non-blocking LED fault indicator for AVR, ESP32, STM32, RP2040
// Author  : Dhaivat Joshi
// License : MIT
// ─────────────────────────────────────────────────────────────

/**
 * @file FaultWatch.h
 * @brief Non-blocking LED fault indicator library.
 *
 * Drives a single-color or two-color LED to display active fault bits
 * from a configurable-width error register. Each active bit maps to a
 * distinct blink pattern (single-color) or a Dell-style A/B diagnostic
 * blink code (two-color). Bits cycle in round-robin order; a priority-1
 * bit preempts all others.
 *
 * The library never calls `delay()`, allocates heap memory, or uses the
 * `String` class. Call `update()` every `loop()` iteration.
 *
 * @par Minimal example
 * @code
 * #include <FaultWatch.h>
 *
 * FaultWatch singleLED(3);
 * FaultWatch twoColorLED(5, 6);
 *
 * void setup() {
 *     singleLED.setErrorByte(0b00000101);   // bits 0 and 2 active
 *     twoColorLED.setErrorByte(0b00000010); // bit 1 active
 * }
 * void loop() {
 *     singleLED.update();
 *     twoColorLED.update();
 * }
 * @endcode
 *
 * @par Error register width
 * Set `FW_ERROR_BITS` to 8 (default), 16, or 32 as a **compiler flag**
 * so all translation units share the same type:
 * @code
 *   // PlatformIO — platformio.ini:
 *   build_flags = -DFW_ERROR_BITS=16
 *
 *   // Arduino IDE — platform.local.txt:
 *   compiler.cpp.extra_flags=-DFW_ERROR_BITS=16
 * @endcode
 *
 * @par Debug output
 * Define `FW_DEBUG 1` **before** `#include <FaultWatch.h>` to enable
 * internal Serial tracing. When 0, all debug macros compile to nothing —
 * zero flash and zero RAM overhead.
 * @code
 *   #define FW_DEBUG 1
 *   #include <FaultWatch.h>
 * @endcode
 *
 * @version 1.0.0
 * @author  Dhaivat Joshi
 * @date    2026-06-16
 * @license MIT
 */

#ifndef FAULTWATCH_H
#define FAULTWATCH_H

#include <Arduino.h>

// ── Debug printing ───────────────────────────────────────────

/**
 * @defgroup debug Debug macros
 * @brief Compile-time gate on internal Serial tracing.
 * @{
 */

/**
 * @def FW_DEBUG
 * @brief Set to 1 before `#include` to enable debug prints, 0 to disable.
 * When 0, `FW_DEBUG_PRINT` and `FW_DEBUG_PRINTLN` expand to nothing.
 */
#ifndef FW_DEBUG
#define FW_DEBUG 0
#endif

#if FW_DEBUG
  #define FW_DEBUG_PRINT(...)   Serial.print(__VA_ARGS__)   ///< Print if FW_DEBUG == 1, else nothing.
  #define FW_DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__) ///< Println if FW_DEBUG == 1, else nothing.
#else
  #define FW_DEBUG_PRINT(...)
  #define FW_DEBUG_PRINTLN(...)
#endif

/** @} */ // end debug group

// ── Error register width ─────────────────────────────────────

/**
 * @defgroup config Configuration macros
 * @brief Compile-time configuration for register width and derived constants.
 * @{
 */

/**
 * @def FW_ERROR_BITS
 * @brief Width of the error register: 8, 16, or 32.
 *
 * Must be identical across **all** translation units — set it as a
 * compiler flag, not a per-file `#define`. Defaults to 8 (AVR-optimal).
 *
 * | Value | FwErrorType | Max fault bits | Typical platform |
 * |-------|-------------|----------------|-----------------|
 * | 8     | uint8_t     | 8              | AVR (Uno/Nano)  |
 * | 16    | uint16_t    | 16             | ESP8266, Mega   |
 * | 32    | uint32_t    | 32             | ESP32, STM32    |
 */
#ifndef FW_ERROR_BITS
#define FW_ERROR_BITS 8
#endif

#if FW_ERROR_BITS == 8
  typedef uint8_t  FwErrorType; ///< Error register type — matches FW_ERROR_BITS.
  #define FW_ERROR_MASK      0xFFUL         ///< Full-width bitmask for the error register.
#elif FW_ERROR_BITS == 16
  typedef uint16_t FwErrorType;
  #define FW_ERROR_MASK      0xFFFFUL
#elif FW_ERROR_BITS == 32
  typedef uint32_t FwErrorType;
  #define FW_ERROR_MASK      0xFFFFFFFFUL
#else
  #error "FW_ERROR_BITS must be 8, 16, or 32"
#endif

/**
 * @def FW_ERROR_HEX_DIGITS
 * @brief Hex digit count for the full error register (2 / 4 / 8).
 * Used by `printHexPadded()` in the example sketch.
 */
#define FW_ERROR_HEX_DIGITS (FW_ERROR_BITS / 4)

/**
 * @def FW_PRI_BYTES
 * @brief Byte count of the nibble-packed priority array.
 * Two priorities are stored per byte (upper nibble = even bit, lower = odd bit).
 * Formula: `ceil(FW_ERROR_BITS / 2)`.
 */
#define FW_PRI_BYTES  ((FW_ERROR_BITS + 1) / 2)

/**
 * @def FW_CODE_BYTES
 * @brief Byte count of the nibble-packed diagnostic code array.
 * One byte per error bit: upper nibble = color-A count, lower = color-B count.
 */
#define FW_CODE_BYTES  FW_ERROR_BITS

/** @} */ // end config group

// ────────────────────────────────────────────────────────────────────────────

/**
 * @class FaultWatch
 * @brief Non-blocking fault indicator for single-color or two-color LEDs.
 *
 * Each instance manages one LED channel and cycles through all active bits
 * in its error register, displaying a pattern for each active fault:
 *
 * - **Single-color** — each of the 8 bit positions maps to a distinct blink
 *   pattern (see setErrorByte() for the pattern table).
 * - **Two-color** — each bit has an A/B blink code: color-A LED blinks
 *   N times, a gap pause, then color-B LED blinks M times, long pause,
 *   repeat. This mimics Dell's diagnostic light scheme.
 *
 * @par Priority rules
 * - Priority 1 = **exclusive lock**: when that bit is active it is always
 *   shown, no round-robin cycling occurs until the bit is cleared.
 * - Priorities 2–8 = **round-robin**: all active bits cycle in order.
 * - Default: every bit starts at priority 2.
 *
 * @par RAM footprint at default (8-bit)
 * `sizeof(FaultWatch)` ≈ 40 bytes. Grows to 52 (16-bit) or
 * 80 (32-bit) as `FW_ERROR_BITS` increases.
 *
 * @par Thread / ISR safety
 * Not ISR-safe. Call only from the main loop.
 */
class FaultWatch {
public:

    /**
     * @brief LED hardware type — set by the constructor, read-only thereafter.
     */
    enum LEDType : uint8_t {
        SINGLE_COLOR = 0, ///< One LED pin — blink patterns only.
        TWO_COLOR    = 1  ///< Two LED pins — A/B diagnostic blink codes.
    };

    // ── Constructors ─────────────────────────────────────────

    /**
     * @brief Construct a single-color LED indicator.
     * @param pin Arduino digital pin number connected to the LED.
     */
    FaultWatch(int pin);

    /**
     * @brief Construct a two-color LED indicator.
     * @param pin1 Arduino pin for color-A (first LED).
     * @param pin2 Arduino pin for color-B (second LED).
     */
    FaultWatch(int pin1, int pin2);

    // ── Manual pattern setters ────────────────────────────────

    void singleFlash(unsigned long interval);  ///< 1 on/off cycle per period.
    void dualBlink(unsigned long interval);    ///< 2 on/off cycles per period.
    void solid();                              ///< LED on continuously.
    void fastFlash(unsigned long interval);    ///< 8 on/off cycles per period.
    void slowDualBlink(unsigned long interval);///< 4 on/off cycles per period.

    // ── Main update ───────────────────────────────────────────

    /**
     * @brief Drive the LED — call every `loop()` iteration. Never blocks.
     *
     * 1. Checks for a priority-1 exclusive bit and preempts if found.
     * 2. When the current pattern completes and the 3 s inter-error pause
     *    elapses, advances to the next active bit.
     * 3. Runs the pattern or code state machine for the current bit.
     */
    void update();

    // ── Error register ────────────────────────────────────────

    /**
     * @brief Set the active fault bits.
     *
     * Each `1` bit represents an active fault. The indicator cycles through
     * all set bits. When all bits are 0 the LED turns off.
     *
     * @param errorBits Bitmask of active faults. Width is `FW_ERROR_BITS`.
     *
     * @par Single-color bit-to-pattern mapping
     * | Bit | Pattern         | Interval  |
     * |-----|-----------------|-----------|
     * | 0   | Single flash    | 500 ms    |
     * | 1   | Dual blink      | 150 ms    |
     * | 2   | Fast flash      | 50 ms     |
     * | 3   | Slow dual blink | 2000 ms   |
     * | 4   | Fast flash      | 50 ms     |
     * | 5   | Fast flash      | 100 ms    |
     * | 6   | Single flash    | 1000 ms   |
     * | 7   | Dual blink      | 250 ms    |
     *
     * For registers wider than 8 bits, bit index wraps modulo 8.
     */
    void setErrorByte(FwErrorType errorBits);

    /**
     * @brief Read the current error register masked to `FW_ERROR_BITS` width.
     * @return Masked copy of the internal error register.
     */
    FwErrorType getErrorHex() const;

    // ── Priority ──────────────────────────────────────────────

    /**
     * @brief Set the display priority for one error bit.
     *
     * | Priority | Behaviour |
     * |----------|-----------|
     * | 1        | Exclusive lock — shown alone until cleared. |
     * | 2–8      | Round-robin — cycles with other active bits. |
     *
     * @param bit      Error bit index (0 to `FW_ERROR_BITS - 1`).
     * @param priority Priority value 1–8. Out-of-range values are ignored.
     */
    void    setPriority(uint8_t bit, uint8_t priority);

    /**
     * @brief Read the display priority for one error bit.
     * @param bit Error bit index (0 to `FW_ERROR_BITS - 1`).
     * @return Priority 1–8, or 0 if `bit` is out of range.
     */
    uint8_t getPriority(uint8_t bit) const;

    // ── Two-color diagnostic codes ────────────────────────────

    /**
     * @brief Set the A/B blink counts for a two-color diagnostic code.
     *
     * Sequence when the bit is active:
     * color-A blinks `colorACount` times → gap → color-B blinks
     * `colorBCount` times → 3 s pause → repeat.
     *
     * Counts capped at 15 (nibble-packed). Default: bit N → A=1, B=(N%8)+1.
     *
     * @param bit           Error bit index (0 to `FW_ERROR_BITS - 1`).
     * @param colorACount   Number of color-A blinks (0–15).
     * @param colorBCount   Number of color-B blinks (0–15).
     */
    void setErrorCode(uint8_t bit, uint8_t colorACount, uint8_t colorBCount);

    /**
     * @brief Set the blink and gap timing for two-color diagnostic codes.
     * @param blinkIntervalMs Per-blink on/off duration (ms). Default: 300.
     * @param gapIntervalMs   A-to-B group pause (ms). Default: 600.
     */
    void setCodeTiming(unsigned long blinkIntervalMs, unsigned long gapIntervalMs);

    /**
     * @brief Read the diagnostic code for a bit, packed as `0xAABB`.
     * @param bit Error bit index (0 to `FW_ERROR_BITS - 1`).
     * @return Packed code (e.g. "2,1" → `0x0201`), or 0 if out of range.
     */
    uint16_t getDiagnosticCodeHex(uint8_t bit) const;

private:
    static const uint8_t  kFlagLedState    = 0x01; ///< Current LED on/off state.
    static const uint8_t  kFlagPatComplete = 0x02; ///< Pattern sequence finished.
    static const uint8_t  kFlagFirstError  = 0x04; ///< Pattern started for current bit.
    static const uint16_t kPatternPauseMs  = 3000; ///< Inter-error pause (ms).

    LEDType _ledType;   ///< SINGLE_COLOR or TWO_COLOR.
    uint8_t _pin1;      ///< Single-color pin, or color-A pin for two-color.
    uint8_t _pin2;      ///< Color-B pin (two-color only).

    uint8_t  _flags;              ///< Packed boolean flags (see kFlag* constants).
    uint8_t  _pattern;            ///< Active pattern id: 0=none, 1–5=patterns.
    uint8_t  _flashCount;         ///< Toggle count within the current pattern run.
    uint8_t  _currentErrorIndex;  ///< Error bit currently being displayed.
    uint16_t _interval;           ///< Toggle period for the current pattern (ms).

    FwErrorType _errorByte;       ///< Active fault bits (width = FW_ERROR_BITS).

    unsigned long _previousMillis;    ///< Time of the last LED toggle (ms).
    unsigned long _errorStartMillis;  ///< When the current pattern completed (ms).

    uint8_t _priority[FW_PRI_BYTES]; ///< Nibble-packed priorities (2 per byte).

    /// Phases of a two-color diagnostic blink sequence.
    enum CodeState : uint8_t {
        CODE_BLINK_A = 0, ///< Blinking color-A LED.
        CODE_GAP_MID = 1, ///< Pause between A and B groups.
        CODE_BLINK_B = 2  ///< Blinking color-B LED.
    };
    uint8_t  _codeState;          ///< Current phase of the blink-code sequence.
    uint16_t _codeBlinkInterval;  ///< Duration of each individual blink (ms).
    uint16_t _codeGapInterval;    ///< Gap between color-A and color-B groups (ms).

    uint8_t _code[FW_CODE_BYTES]; ///< Nibble-packed codes: upper=A count, lower=B count.

    void   _applyPattern(uint8_t id, uint16_t intervalMs);
    void   handleErrorPattern(int errorIndex);
    void   handleUpdatePattern(int errorIndex);
    void   setLEDState(bool on1, bool on2);
    int8_t findExclusiveActiveBit() const;
    int8_t nextActiveBit() const;
    void   startCodeDisplay(int errorIndex);
    void   updateCodeDisplay(int errorIndex);
};

#endif // FAULTWATCH_H
