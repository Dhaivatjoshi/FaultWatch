// ─────────────────────────────────────────────────────────────
// FaultWatch  v1.0.0
// Non-blocking LED fault indicator for AVR, ESP32, STM32, RP2040
// Author  : Dhaivat Joshi
// License : MIT
// ─────────────────────────────────────────────────────────────
#include "FaultWatch.h"

// ── Nibble-pack helpers (file-scope, not exported) ───────────────────────────
// Priority: even bit → upper nibble, odd bit → lower nibble of _priority[bit/2]
#define PRI_GET(arr, bit) \
    (((bit) & 1) ? ((arr)[(bit) >> 1] & 0x0F) \
                 : (((arr)[(bit) >> 1] >> 4) & 0x0F))

#define PRI_SET(arr, bit, p) do { \
    if ((bit) & 1) (arr)[(bit) >> 1] = ((arr)[(bit) >> 1] & 0xF0) | ((p) & 0x0F); \
    else           (arr)[(bit) >> 1] = ((arr)[(bit) >> 1] & 0x0F) | (((p) & 0x0F) << 4); \
} while (0)

// Diagnostic codes: upper nibble = colorA count, lower nibble = colorB count
#define CODE_A(b)      (((b) >> 4) & 0x0F)
#define CODE_B(b)      ((b) & 0x0F)
#define CODE_PACK(a,b) ((uint8_t)((((a) & 0x0F) << 4) | ((b) & 0x0F)))

// ── Pattern lookup tables ────────────────────────────────────────────────────

/** @brief Maps error bit index (% 8) to a pattern id and toggle interval. */
struct PatternEntry { uint8_t id; uint16_t interval; };

/** @brief Bit-index → pattern lookup. Replaces an 8-case switch statement. */
static const PatternEntry kBitPatterns[8] = {
    {1,  500},   // bit 0: singleFlash(500ms)
    {2,  150},   // bit 1: dualBlink(150ms)
    {4,   50},   // bit 2: fastFlash(50ms)
    {5, 2000},   // bit 3: slowDualBlink(2000ms)
    {4,   50},   // bit 4: fastFlash(50ms)
    {4,  100},   // bit 5: fastFlash(100ms)
    {1, 1000},   // bit 6: singleFlash(1000ms)
    {2,  250},   // bit 7: dualBlink(250ms)
};

/** @brief Toggle count that completes each pattern (indexed by pattern id 0–5).
 *  Pattern 3 (solid) is handled separately and is not in this table. */
static const uint8_t kMaxFlash[6] = {0, 2, 4, 0, 8, 4};

// ── Shared constructor body ──────────────────────────────────────────────────
/** @brief Shared initialisation for both constructors.
 *  Sets all priorities to 2 (round-robin) and codes to default (A=1, B=bit+1). */
static void initInstance(FaultWatch::LEDType /*type*/,
                         uint8_t *priority, uint8_t *code)
{
    // All bits default to priority 2: pack 0x22 per byte (even=2, odd=2)
    for (uint8_t i = 0; i < FW_PRI_BYTES; i++)
        priority[i] = 0x22;

    // Default code for bit N: A=1, B=((N%8)+1), packed as nibbles
    for (uint8_t i = 0; i < FW_CODE_BYTES; i++)
        code[i] = CODE_PACK(1, (i % 8) + 1);
}

// ── Constructors ─────────────────────────────────────────────────────────────
FaultWatch::FaultWatch(int pin)
    : _ledType(SINGLE_COLOR),
      _pin1((uint8_t)pin), _pin2(0),
      _flags(0), _pattern(0), _flashCount(0), _currentErrorIndex(0),
      _interval(0), _errorByte(0),
      _previousMillis(0), _errorStartMillis(0),
      _codeState(CODE_BLINK_A), _codeBlinkInterval(300), _codeGapInterval(600)
{
    pinMode(_pin1, OUTPUT);
    initInstance(_ledType, _priority, _code);
    digitalWrite(_pin1, LOW);
}

FaultWatch::FaultWatch(int pin1, int pin2)
    : _ledType(TWO_COLOR),
      _pin1((uint8_t)pin1), _pin2((uint8_t)pin2),
      _flags(0), _pattern(0), _flashCount(0), _currentErrorIndex(0),
      _interval(0), _errorByte(0),
      _previousMillis(0), _errorStartMillis(0),
      _codeState(CODE_BLINK_A), _codeBlinkInterval(300), _codeGapInterval(600)
{
    pinMode(_pin1, OUTPUT);
    pinMode(_pin2, OUTPUT);
    initInstance(_ledType, _priority, _code);
    digitalWrite(_pin1, LOW);
    digitalWrite(_pin2, LOW);
}

// ── Pattern setters (public — thin wrappers) ─────────────────────────────────
void FaultWatch::singleFlash(unsigned long interval)   { _applyPattern(1, (uint16_t)interval); }
void FaultWatch::dualBlink(unsigned long interval)     { _applyPattern(2, (uint16_t)interval); }
void FaultWatch::solid()                               { _applyPattern(3, 0); }
void FaultWatch::fastFlash(unsigned long interval)     { _applyPattern(4, (uint16_t)interval); }
void FaultWatch::slowDualBlink(unsigned long interval) { _applyPattern(5, (uint16_t)interval); }

/** @brief Set pattern id + interval, reset flash counter, clear completion flag. */
void FaultWatch::_applyPattern(uint8_t id, uint16_t intervalMs)
{
    _pattern    = id;
    _interval   = intervalMs;
    _flashCount = 0;
    _flags &= ~kFlagPatComplete;
}

// ── Main update ──────────────────────────────────────────────────────────────
void FaultWatch::update()
{
    unsigned long now = millis();

    // Priority-1 (exclusive) preemption: if a higher-priority bit becomes
    // active and we are not already showing it, take over immediately.
    int8_t exclusive = findExclusiveActiveBit();
    if (exclusive >= 0 && (uint8_t)exclusive != _currentErrorIndex)
    {
        _currentErrorIndex = (uint8_t)exclusive;
        _flags &= ~(kFlagFirstError | kFlagPatComplete);
        _flashCount = 0;
        setLEDState(false, false);
        FW_DEBUG_PRINT(F("FaultWatch: exclusive -> bit "));
        FW_DEBUG_PRINTLN(_currentErrorIndex);
    }

    // When a pattern finishes and the inter-error pause has elapsed,
    // advance to the next error (or repeat if exclusive is still active).
    if ((_flags & kFlagPatComplete) && (now - _errorStartMillis >= kPatternPauseMs))
    {
        _errorStartMillis = now;
        setLEDState(false, false);
        _flags &= ~kFlagPatComplete;
        _flashCount = 0;

        int8_t next = (exclusive >= 0) ? exclusive : nextActiveBit();
        if (next >= 0)
        {
            _currentErrorIndex = (uint8_t)next;
            _flags &= ~kFlagFirstError;
            FW_DEBUG_PRINT(F("FaultWatch: next bit = "));
            FW_DEBUG_PRINTLN(_currentErrorIndex);
            handleErrorPattern(_currentErrorIndex);
        }
        else
        {
            FW_DEBUG_PRINTLN(F("FaultWatch: no active errors"));
            _currentErrorIndex = 0;
            setLEDState(false, false);
        }
    }

    if (!(_flags & kFlagPatComplete))
    {
        if (_errorByte & ((FwErrorType)1 << _currentErrorIndex))
        {
            if (!(_flags & kFlagFirstError))
            {
                _flags |= kFlagFirstError;
                handleErrorPattern(_currentErrorIndex);
            }
            handleUpdatePattern(_currentErrorIndex);
        }
        else
        {
            // Current error cleared mid-pattern — find replacement.
            int8_t next = (exclusive >= 0) ? exclusive : nextActiveBit();
            if (next >= 0)
            {
                _currentErrorIndex = (uint8_t)next;
                _flags &= ~kFlagFirstError;
            }
            else
            {
                _currentErrorIndex = 0;
                setLEDState(false, false);
            }
        }
    }
}

// ── Bit scanners (bare-metal, fixed iterations, no allocation) ───────────────
/** @brief Return the index of the first active bit with priority 1, or -1. */
int8_t FaultWatch::findExclusiveActiveBit() const
{
    for (uint8_t i = 0; i < FW_ERROR_BITS; i++)
    {
        if ((_errorByte & ((FwErrorType)1 << i)) && PRI_GET(_priority, i) == 1)
            return (int8_t)i;
    }
    return -1;
}

/** @brief Return the index of the next active bit after _currentErrorIndex (wraps), or -1. */
int8_t FaultWatch::nextActiveBit() const
{
    for (uint8_t i = 1; i <= FW_ERROR_BITS; i++)
    {
        uint8_t idx = (_currentErrorIndex + i) % FW_ERROR_BITS;
        if (_errorByte & ((FwErrorType)1 << idx))
            return (int8_t)idx;
    }
    return -1;
}

// ── Priority API ─────────────────────────────────────────────────────────────
void FaultWatch::setPriority(uint8_t bit, uint8_t priority)
{
    if (bit >= FW_ERROR_BITS || priority < 1 || priority > 8) return;
    PRI_SET(_priority, bit, priority);
}

uint8_t FaultWatch::getPriority(uint8_t bit) const
{
    if (bit >= FW_ERROR_BITS) return 0;
    return PRI_GET(_priority, bit);
}

// ── Error register API ───────────────────────────────────────────────────────
void FaultWatch::setErrorByte(FwErrorType errorBits)
{
    FW_DEBUG_PRINT(F("FaultWatch: setErrorByte = 0x"));
    FW_DEBUG_PRINTLN((unsigned long)errorBits, HEX);
    _errorByte = errorBits;
    handleErrorPattern(_currentErrorIndex);
}

FwErrorType FaultWatch::getErrorHex() const
{
    FwErrorType masked = _errorByte & (FwErrorType)FW_ERROR_MASK;
    FW_DEBUG_PRINT(_ledType == TWO_COLOR
        ? F("FaultWatch: DOUBLE error hex = 0x")
        : F("FaultWatch: SINGLE error hex = 0x"));
    FW_DEBUG_PRINTLN((unsigned long)masked, HEX);
    return masked;
}

// ── TWO_COLOR code API ───────────────────────────────────────────────────────
void FaultWatch::setErrorCode(uint8_t bit, uint8_t colorACount, uint8_t colorBCount)
{
    if (bit >= FW_ERROR_BITS) return;
    // Clamp to nibble range (0-15); counts above 15 are not physically useful
    if (colorACount > 15) colorACount = 15;
    if (colorBCount > 15) colorBCount = 15;
    _code[bit] = CODE_PACK(colorACount, colorBCount);
}

void FaultWatch::setCodeTiming(unsigned long blinkIntervalMs, unsigned long gapIntervalMs)
{
    _codeBlinkInterval = (uint16_t)blinkIntervalMs;
    _codeGapInterval   = (uint16_t)gapIntervalMs;
}

uint16_t FaultWatch::getDiagnosticCodeHex(uint8_t bit) const
{
    if (bit >= FW_ERROR_BITS) return 0;
    return ((uint16_t)CODE_A(_code[bit]) << 8) | CODE_B(_code[bit]);
}

// ── LED output helpers ───────────────────────────────────────────────────────
/** @brief Drive LED pin(s) and keep kFlagLedState in sync with pin1's physical state. */
void FaultWatch::setLEDState(bool on1, bool on2)
{
    // Keep kFlagLedState in sync with pin1's physical state
    if (on1) _flags |=  kFlagLedState;
    else     _flags &= ~kFlagLedState;
    digitalWrite(_pin1, on1 ? HIGH : LOW);
    if (_ledType == TWO_COLOR) digitalWrite(_pin2, on2 ? HIGH : LOW);
}

// ── Pattern engine ───────────────────────────────────────────────────────────
/** @brief Start the pattern or code sequence for the given error bit. */
void FaultWatch::handleErrorPattern(int errorIndex)
{
    if (!(_errorByte & ((FwErrorType)1 << errorIndex))) return;

    if (_ledType == TWO_COLOR)
    {
        startCodeDisplay(errorIndex);
        return;
    }

    // Lookup table replaces 8-case switch — expandable by editing kBitPatterns
    const PatternEntry &p = kBitPatterns[errorIndex % 8];
    _applyPattern(p.id, p.interval);
}

/** @brief Advance the running pattern or code sequence by one tick. */
void FaultWatch::handleUpdatePattern(int errorIndex)
{
    if (_ledType == TWO_COLOR)
    {
        updateCodeDisplay(errorIndex);
        return;
    }

    unsigned long now = millis();

    // Pattern 3 = solid: turn on immediately, mark done
    if (_pattern == 3)
    {
        setLEDState(true, false);
        _flags |= kFlagPatComplete;
        _errorStartMillis = now;
        return;
    }

    // All other patterns: toggle at _interval until kMaxFlash count reached
    uint8_t maxF = (_pattern < 6) ? kMaxFlash[_pattern] : 0;

    if (_flashCount < maxF && (now - _previousMillis >= _interval))
    {
        _previousMillis = now;
        _flags ^= kFlagLedState;
        digitalWrite(_pin1, (_flags & kFlagLedState) ? HIGH : LOW);
        _flashCount++;
    }

    if (_flashCount >= maxF)
    {
        setLEDState(false, false);   // LED off before inter-error pause
        _flags |= kFlagPatComplete;
        _errorStartMillis = now;
    }
}

// ── TWO_COLOR diagnostic code state machine ──────────────────────────────────
/** @brief Reset the two-color code state machine to phase CODE_BLINK_A. */
void FaultWatch::startCodeDisplay(int errorIndex)
{
    (void)errorIndex;
    _codeState  = CODE_BLINK_A;
    _flashCount = 0;
    _flags     &= ~kFlagLedState;
    _previousMillis = millis();
    setLEDState(false, false);
}

/** @brief Tick the two-color blink-code state machine (BLINK_A → GAP_MID → BLINK_B → done). */
void FaultWatch::updateCodeDisplay(int errorIndex)
{
    unsigned long now = millis();

    if (_codeState == CODE_GAP_MID)
    {
        if (now - _previousMillis >= _codeGapInterval)
        {
            _previousMillis = now;
            _flashCount = 0;
            _codeState  = CODE_BLINK_B;
        }
        return;
    }

    uint8_t raw    = _code[errorIndex];
    uint8_t target = (_codeState == CODE_BLINK_A) ? CODE_A(raw) : CODE_B(raw);
    uint8_t pin    = (_codeState == CODE_BLINK_A) ? _pin1 : _pin2;

    // Skip phases with 0 blinks
    if (target == 0)
    {
        digitalWrite(pin, LOW);
        _flashCount = 0;
        _previousMillis = now;
        if (_codeState == CODE_BLINK_A) _codeState = CODE_GAP_MID;
        else { _flags |= kFlagPatComplete; _errorStartMillis = now; }
        return;
    }

    if (now - _previousMillis >= _codeBlinkInterval)
    {
        _previousMillis = now;
        _flags ^= kFlagLedState;
        bool ls = (_flags & kFlagLedState) != 0;
        digitalWrite(pin, ls ? HIGH : LOW);
        if (!ls) _flashCount++;   // count completed on/off cycles
    }

    if (_flashCount >= target)
    {
        digitalWrite(pin, LOW);
        _flashCount = 0;
        _previousMillis = now;
        if (_codeState == CODE_BLINK_A) _codeState = CODE_GAP_MID;
        else { _flags |= kFlagPatComplete; _errorStartMillis = now; }
    }
}
