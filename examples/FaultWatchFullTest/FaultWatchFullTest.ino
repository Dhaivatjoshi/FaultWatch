// ─────────────────────────────────────────────────────────────
// FaultWatch v1.0.0 — Full Test Sketch
//
// Exercises:
//   - SINGLE_COLOR LED  (1 pin)
//   - TWO_COLOR LED     (2 pins)
//   - All 8 error bits / blink patterns
//   - Priority system (setPriority / getPriority)
//   - Two-color diagnostic codes (setErrorCode / setCodeTiming)
//   - getErrorHex() / getDiagnosticCodeHex()
//   - FW_DEBUG output
//
// Wiring:
//   Pin 3        -> single-color LED (+ resistor) -> GND
//   Pin 5        -> two-color LED color A (+ resistor) -> GND
//   Pin 6        -> two-color LED color B (+ resistor) -> GND
//
// FW_DEBUG must be defined BEFORE #include <FaultWatch.h>
// ─────────────────────────────────────────────────────────────

#define FW_DEBUG 1

// ── Optional: widen the error register ──────────────────────
// NOTE: must also be set as a compiler flag so FaultWatch.cpp
// sees the same value — defining it here alone is NOT sufficient.
// #define FW_ERROR_BITS 16
// #define FW_ERROR_BITS 32

#include <FaultWatch.h>

FaultWatch singleLED(3);
FaultWatch twoColorLED(5, 6);

byte singleErrorByte   = 0b00000001;
byte twoColorErrorByte = 0b00000000;

// ── Zero-allocation hex printer ──────────────────────────────
// Prints `value` as exactly `digits` hex chars, zero-padded.
// No String, no snprintf — pure nibble loop, works on all platforms.
void printHexPadded(unsigned long value, uint8_t digits) {
  for (int8_t i = digits - 1; i >= 0; i--) {
    Serial.print((uint8_t)((value >> (i * 4)) & 0xF), HEX);
  }
}

void printHelp() {
  Serial.println();
  Serial.println(F("=== FaultWatch Full Test ==="));
  Serial.println(F("SINGLE_COLOR bit->pattern:"));
  Serial.println(F("  0->singleFlash(500)  1->dualBlink(150)  2->fastFlash(50)"));
  Serial.println(F("  3->slowDual(2000)    4->fastFlash(50)   5->fastFlash(100)"));
  Serial.println(F("  6->singleFlash(1000) 7->dualBlink(250)"));
  Serial.println(F("TWO_COLOR default codes: bit N -> (1,(N%8)+1). Max count=15."));
  Serial.println();
  Serial.println(F("Commands (values decimal unless noted):"));
  Serial.println(F("  s<0-255>       set SINGLE error byte  (e.g. s5)"));
  Serial.println(F("  t<0-255>       set DOUBLE error byte  (e.g. t128)"));
  Serial.println(F("  p<bit>,<1-8>   set priority (both LEDs) (e.g. p0,1)"));
  Serial.println(F("  c<bit>,<A>,<B> set TWO_COLOR code, A/B 0-15 (e.g. c0,2,1)"));
  Serial.println(F("  g<blink>,<gap> set code timing ms (e.g. g300,600)"));
  Serial.println(F("  e              print error registers as hex"));
  Serial.println(F("  d<bit>         print TWO_COLOR code hex (e.g. d0)"));
  Serial.println(F("  h              this help"));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  printHelp();

  // ── Optional: priority configuration ─────────────────────────
  // All bits default to priority 2 (non-exclusive, round-robin).
  // Uncomment to make bit 0 EXCLUSIVE on both indicators:
  // singleLED.setPriority(0, 1);
  // twoColorLED.setPriority(0, 1);

  // ── Optional: custom Dell-style diagnostic codes ──────────────
  // Default code for bit N is (1, (N%8)+1).
  // Uncomment to assign real fault codes, e.g.:
  //   bit0 -> "2,1" processor failure
  //   bit1 -> "2,2" BIOS / ROM failure
  //   bit2 -> "3,1" coin-cell battery failure
  // twoColorLED.setErrorCode(0, 2, 1);
  // twoColorLED.setErrorCode(1, 2, 2);
  // twoColorLED.setErrorCode(2, 3, 1);

  // ── Optional: custom blink/gap timing ────────────────────────
  // twoColorLED.setCodeTiming(300, 800);

  singleLED.setErrorByte(singleErrorByte);
  twoColorLED.setErrorByte(twoColorErrorByte);

  Serial.print(F("SINGLE init = 0b")); Serial.println(singleErrorByte,   BIN);
  Serial.print(F("DOUBLE init = 0b")); Serial.println(twoColorErrorByte, BIN);
}

void handleSerialCommand() {
  // Fixed char buffer — no String, no heap allocation
  char buf[20];
  uint8_t len = (uint8_t)Serial.readBytesUntil('\n', buf, sizeof(buf) - 1);
  while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == ' ')) len--;
  buf[len] = '\0';
  if (len == 0) return;

  char  cmd  = (char)(buf[0] | 0x20); // normalise to lowercase
  char *rest = buf + 1;
  char *ep;                            // end-pointer for strtol

  switch (cmd) {

  case 'h':
    printHelp();
    break;

  case 's': {
    long v = strtol(rest, NULL, 10);
    if (v < 0 || v > 255) { Serial.println(F("Invalid. 0-255")); break; }
    singleErrorByte = (byte)v;
    singleLED.setErrorByte(singleErrorByte);
    Serial.print(F("SINGLE = 0b")); Serial.println(singleErrorByte, BIN);
    break;
  }

  case 't': {
    long v = strtol(rest, NULL, 10);
    if (v < 0 || v > 255) { Serial.println(F("Invalid. 0-255")); break; }
    twoColorErrorByte = (byte)v;
    twoColorLED.setErrorByte(twoColorErrorByte);
    Serial.print(F("DOUBLE = 0b")); Serial.println(twoColorErrorByte, BIN);
    break;
  }

  case 'p': {
    // p<bit>,<priority>
    long bit = strtol(rest, &ep, 10);
    if (*ep != ',' || bit < 0 || bit > 7) { Serial.println(F("Usage: p<bit>,<1-8>")); break; }
    long pri = strtol(ep + 1, NULL, 10);
    if (pri < 1 || pri > 8) { Serial.println(F("Priority 1-8")); break; }
    singleLED.setPriority((uint8_t)bit, (uint8_t)pri);
    twoColorLED.setPriority((uint8_t)bit, (uint8_t)pri);
    Serial.print(F("bit ")); Serial.print((int)bit);
    Serial.print(F(" -> pri ")); Serial.println((int)pri);
    break;
  }

  case 'c': {
    // c<bit>,<A>,<B>  — A and B capped at 15 (nibble storage)
    long bit = strtol(rest, &ep, 10);
    if (*ep != ',' || bit < 0 || bit > 7) { Serial.println(F("Usage: c<bit>,<A>,<B>")); break; }
    long a = strtol(ep + 1, &ep, 10);
    if (*ep != ',' || a < 0 || a > 15) { Serial.println(F("A: 0-15")); break; }
    long b = strtol(ep + 1, NULL, 10);
    if (b < 0 || b > 15) { Serial.println(F("B: 0-15")); break; }
    twoColorLED.setErrorCode((uint8_t)bit, (uint8_t)a, (uint8_t)b);
    Serial.print(F("code bit ")); Serial.print((int)bit);
    Serial.print(F(" = ")); Serial.print((int)a);
    Serial.print(F(",")); Serial.println((int)b);
    break;
  }

  case 'g': {
    // g<blinkMs>,<gapMs>
    long blink = strtol(rest, &ep, 10);
    if (*ep != ',' || blink <= 0) { Serial.println(F("Usage: g<blink>,<gap>")); break; }
    long gap = strtol(ep + 1, NULL, 10);
    if (gap <= 0) { Serial.println(F("gap > 0")); break; }
    twoColorLED.setCodeTiming((unsigned long)blink, (unsigned long)gap);
    Serial.print(F("timing blink=")); Serial.print(blink);
    Serial.print(F("ms gap=")); Serial.print(gap); Serial.println(F("ms"));
    break;
  }

  case 'e': {
    // Print error registers, zero-padded to FW_ERROR_HEX_DIGITS
    Serial.print(F("SINGLE = 0x"));
    printHexPadded(singleLED.getErrorHex(), FW_ERROR_HEX_DIGITS);
    Serial.println();
    Serial.print(F("DOUBLE = 0x"));
    printHexPadded(twoColorLED.getErrorHex(), FW_ERROR_HEX_DIGITS);
    Serial.println();
    break;
  }

  case 'd': {
    // d<bit> — print TWO_COLOR diagnostic code as 0xAABB
    long bit = strtol(rest, NULL, 10);
    if (bit < 0 || bit > 7) { Serial.println(F("Usage: d<bit 0-7>")); break; }
    uint16_t diag = twoColorLED.getDiagnosticCodeHex((uint8_t)bit);
    Serial.print(F("bit ")); Serial.print((int)bit);
    Serial.print(F(" code = 0x")); printHexPadded(diag, 4); Serial.println();
    break;
  }

  default:
    Serial.println(F("Unknown cmd. 'h' for help."));
  }
}

void loop() {
  if (Serial.available() > 0) handleSerialCommand();
  singleLED.update();
  twoColorLED.update();
}
