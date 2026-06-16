#!/usr/bin/env python3
"""
FaultWatch — Serial Automation Test
===========================================
Connects to an Arduino running FaultWatchFullTest.ino,
exercises every library function through serial commands, verifies
the responses, and prints a function-level checklist at the end.

Requirements:
    pip install pyserial

Usage:
    python testLedErrorIndicatorFullTest.py [PORT] [BAUD]

    PORT  Serial port. Default: COM3 (Windows) or /dev/ttyUSB0 (Linux/Mac).
    BAUD  Baud rate.  Default: 115200

Exit code: 0 = all tests passed, 1 = one or more failed.
"""

import sys
import time
import re
import argparse
import datetime
import os
import serial

__version__ = "1.0.0"

# ── Default connection settings ──────────────────────────────
DEFAULT_PORT = "COM9"
DEFAULT_BAUD = 115200
CMD_WAIT_S   = 0.30   # seconds to let Arduino respond after sending a command
DRAIN_CHUNK  = 0.05   # seconds between in_waiting polls when draining
BOOT_WAIT_S  = 2.0    # seconds for Arduino to finish reset after serial open

LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "testFaultWatchFullTest.log")


# ────────────────────────────────────────────────────────────────────────────
# Tee: mirror stdout to an append-mode log file
# ────────────────────────────────────────────────────────────────────────────
class _Tee:
    """Writes every print() call to both the real stdout and a log file."""
    def __init__(self, log_path: str):
        self._console = sys.stdout
        self._log     = open(log_path, "a", encoding="utf-8")
        sys.stdout    = self

    def write(self, data: str):
        self._console.write(data)
        self._log.write(data)

    def flush(self):
        self._console.flush()
        self._log.flush()

    def close(self):
        sys.stdout = self._console
        self._log.close()


def _open_log() -> "_Tee":
    tee = _Tee(LOG_FILE)
    ts  = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"\n{'#' * 60}")
    print(f"# Run: {ts}")
    print(f"{'#' * 60}")
    return tee


# ────────────────────────────────────────────────────────────────────────────
# Data types
# ────────────────────────────────────────────────────────────────────────────
class TestResult:
    """One assertion outcome."""
    def __init__(self, name: str, function: str, passed: bool, detail: str = ""):
        self.name     = name
        self.function = function
        self.passed   = passed
        self.detail   = detail


# ────────────────────────────────────────────────────────────────────────────
# Serial helper
# ────────────────────────────────────────────────────────────────────────────
class ArduinoSerial:
    """Thin wrapper around pyserial for the tester."""

    def __init__(self, port: str, baud: int):
        self.ser = serial.Serial(port, baud, timeout=1.0)
        print(f"  Connected to {port} @ {baud} baud")
        time.sleep(BOOT_WAIT_S)          # wait for Arduino bootloader + setup()
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def send(self, cmd: str, wait: float = CMD_WAIT_S) -> str:
        """
        Send `cmd + newline`, collect all bytes received within `wait`
        seconds, return as a stripped string.  No String heap on the
        Python side either — bytes only, decoded once at the end.
        """
        self.ser.reset_input_buffer()
        self.ser.write((cmd + '\n').encode('ascii'))
        time.sleep(wait)

        raw = b''
        while self.ser.in_waiting:
            raw += self.ser.read(self.ser.in_waiting)
            time.sleep(DRAIN_CHUNK)

        return raw.decode('utf-8', errors='replace').strip()


# ────────────────────────────────────────────────────────────────────────────
# Test runner
# ────────────────────────────────────────────────────────────────────────────
class LEDTester:

    def __init__(self, port: str, baud: int):
        self.dev     = ArduinoSerial(port, baud)
        self.results: list[TestResult] = []

    def close(self):
        self.dev.close()

    # ── Assertion helper ────────────────────────────────────────
    def check(self,
              test_name:  str,
              function:   str,
              cmd:        str,
              expect:     str,
              mode:       str = "contains",
              wait:       float = CMD_WAIT_S) -> bool:
        """
        Send `cmd`, compare response to `expect`.
        mode:
          "contains" — expect substring (case-insensitive) is in response
          "regex"    — expect is a regex pattern (re.IGNORECASE)
        """
        response = self.dev.send(cmd, wait=wait)

        if mode == "contains":
            passed = expect.lower() in response.lower()
        else:  # regex
            passed = bool(re.search(expect, response, re.IGNORECASE))

        self.results.append(TestResult(test_name, function, passed,
                                       f"cmd={cmd!r} expect={expect!r} got={response!r}"))

        tag = "PASS" if passed else "FAIL"
        print(f"    [{tag}] {test_name}")
        if not passed:
            print(f"           cmd     : {cmd!r}")
            print(f"           expected: {expect!r}")
            print(f"           got     : {response!r}")
        return passed

    def manual_check(self, test_name: str, function: str,
                     passed: bool, detail: str = ""):
        """Record a result computed outside check()."""
        self.results.append(TestResult(test_name, function, passed, detail))
        tag = "PASS" if passed else "FAIL"
        print(f"    [{tag}] {test_name}")
        if not passed:
            print(f"           detail: {detail}")

    # ── Test suites ─────────────────────────────────────────────
    def test_help(self):
        print("\n[1] Help command")
        self.check("Help menu contains 'Commands'",
                   "printHelp()",
                   "h", "Commands", wait=0.5)

    def test_set_error_single(self):
        print("\n[2] setErrorByte() — SINGLE LED")
        self.check("s0  → SINGLE = 0b0",
                   "setErrorByte() SINGLE",
                   "s0", "0b0")
        self.check("s5  → SINGLE = 0b101",
                   "setErrorByte() SINGLE",
                   "s5", "0b101")
        self.check("s255 → SINGLE = 0b11111111",
                   "setErrorByte() SINGLE",
                   "s255", "0b11111111")

    def test_set_error_double(self):
        print("\n[3] setErrorByte() — DOUBLE LED")
        self.check("t0   → DOUBLE = 0b0",
                   "setErrorByte() DOUBLE",
                   "t0", "0b0")
        self.check("t128 → DOUBLE = 0b10000000",
                   "setErrorByte() DOUBLE",
                   "t128", "0b10000000")
        self.check("t255 → DOUBLE = 0b11111111",
                   "setErrorByte() DOUBLE",
                   "t255", "0b11111111")

    def test_get_error_hex(self):
        print("\n[4] getErrorHex()")
        # Set known values then read back
        self.dev.send("s5")
        self.dev.send("t2")
        response = self.dev.send("e")

        single_ok = "single = 0x05" in response.lower()
        double_ok = "double = 0x02" in response.lower()

        self.manual_check("s5  → getErrorHex SINGLE = 0x05",
                          "getErrorHex() SINGLE", single_ok, response)
        self.manual_check("t2  → getErrorHex DOUBLE = 0x02",
                          "getErrorHex() DOUBLE", double_ok, response)

        # Max values
        self.dev.send("s255")
        self.dev.send("t255")
        response = self.dev.send("e")
        self.manual_check("s255 → getErrorHex SINGLE = 0xFF",
                          "getErrorHex() SINGLE",
                          "single = 0xff" in response.lower(), response)
        self.manual_check("t255 → getErrorHex DOUBLE = 0xFF",
                          "getErrorHex() DOUBLE",
                          "double = 0xff" in response.lower(), response)

        # Zero values
        self.dev.send("s0")
        self.dev.send("t0")
        response = self.dev.send("e")
        self.manual_check("s0  → getErrorHex SINGLE = 0x00",
                          "getErrorHex() SINGLE",
                          "single = 0x00" in response.lower(), response)

    def test_independence(self):
        print("\n[5] Instance independence (separate error registers)")
        self.dev.send("s170")   # 0xAA
        self.dev.send("t85")    # 0x55
        response = self.dev.send("e")
        s_ok = "single = 0xaa" in response.lower()
        d_ok = "double = 0x55" in response.lower()
        self.manual_check("SINGLE=0xAA and DOUBLE=0x55 are independent",
                          "setErrorByte() independence",
                          s_ok and d_ok, response)

    def test_input_validation(self):
        print("\n[6] Input validation")
        self.check("s256 → rejected (> 255)",
                   "setErrorByte() validation",
                   "s256", "Invalid")
        self.check("t-1  → rejected (< 0)",
                   "setErrorByte() validation",
                   "t-1", "Invalid")
        self.check("Unknown cmd 'z' → rejected",
                   "command parser",
                   "z", "Unknown")

    def test_set_priority(self):
        print("\n[7] setPriority()")
        self.check("p0,1 → bit 0 = priority 1 (exclusive)",
                   "setPriority()",
                   "p0,1", "pri 1")
        self.check("p3,5 → bit 3 = priority 5",
                   "setPriority()",
                   "p3,5", "pri 5")
        self.check("p7,8 → bit 7 = priority 8 (lowest)",
                   "setPriority()",
                   "p7,8", "pri 8")
        # Restore defaults
        for b in range(8):
            self.dev.send(f"p{b},2")

    def test_priority_validation(self):
        print("\n[8] setPriority() validation")
        self.check("p0,0 → rejected (priority 0 invalid)",
                   "setPriority() validation",
                   "p0,0", "Priority 1-8")
        self.check("p0,9 → rejected (priority 9 invalid)",
                   "setPriority() validation",
                   "p0,9", "Priority 1-8")
        self.check("p8,1 → rejected (bit 8 out of range)",
                   "setPriority() validation",
                   "p8,1", "p<bit>")

    def test_set_error_code(self):
        print("\n[9] setErrorCode()")
        self.check("c0,2,1 → code (2,1) accepted",
                   "setErrorCode()",
                   "c0,2,1", "2,1")
        self.check("c3,3,4 → code (3,4) accepted",
                   "setErrorCode()",
                   "c3,3,4", "3,4")
        self.check("c7,1,8 → code (1,8) accepted",
                   "setErrorCode()",
                   "c7,1,8", "1,8")

    def test_get_diagnostic_hex(self):
        print("\n[10] getDiagnosticCodeHex()")
        self.dev.send("c0,2,1")
        self.check("c0,2,1 → d0 = 0x0201",
                   "getDiagnosticCodeHex()",
                   "d0", "0x0201")

        self.dev.send("c3,3,4")
        self.check("c3,3,4 → d3 = 0x0304",
                   "getDiagnosticCodeHex()",
                   "d3", "0x0304")

        self.dev.send("c7,1,8")
        self.check("c7,1,8 → d7 = 0x0108",
                   "getDiagnosticCodeHex()",
                   "d7", "0x0108")

        # Nibble max (15,15) → 0x0F0F
        self.dev.send("c1,15,15")
        self.check("c1,15,15 → d1 = 0x0F0F (nibble max)",
                   "getDiagnosticCodeHex() clamp",
                   "d1", "0x0f0f")

    def test_error_code_validation(self):
        print("\n[11] setErrorCode() validation")
        self.check("c0,16,1 → rejected (A > 15)",
                   "setErrorCode() validation",
                   "c0,16,1", "A: 0-15")
        self.check("c0,1,16 → rejected (B > 15)",
                   "setErrorCode() validation",
                   "c0,1,16", "B: 0-15")
        self.check("c8,1,1  → rejected (bit > 7)",
                   "setErrorCode() validation",
                   "c8,1,1", "Usage: c<bit>")

    def test_code_timing(self):
        print("\n[12] setCodeTiming()")
        self.check("g300,600 → blink=300ms",
                   "setCodeTiming()",
                   "g300,600", "blink=300ms")
        self.check("g300,600 → gap=600ms",
                   "setCodeTiming()",
                   "g300,600", "gap=600ms")
        self.check("g100,300 → blink=100ms",
                   "setCodeTiming()",
                   "g100,300", "blink=100ms")

    def test_code_timing_validation(self):
        print("\n[13] setCodeTiming() validation")
        self.check("g0,600 → rejected (blink = 0)",
                   "setCodeTiming() validation",
                   "g0,600", "Usage: g<blink>")
        self.check("g300,0 → rejected (gap = 0)",
                   "setCodeTiming() validation",
                   "g300,0", "gap > 0")

    def test_roundtrip(self):
        print("\n[14] Full round-trip: set then read back via hex")
        self.dev.send("s0")
        self.dev.send("t0")

        self.dev.send("s170")     # 0xAA
        resp = self.dev.send("e")
        self.manual_check("s170 → getErrorHex SINGLE = 0xAA",
                          "setErrorByte() + getErrorHex()",
                          "single = 0xaa" in resp.lower(), resp)

        self.dev.send("t85")      # 0x55
        resp = self.dev.send("e")
        self.manual_check("t85 → getErrorHex DOUBLE = 0x55",
                          "setErrorByte() + getErrorHex()",
                          "double = 0x55" in resp.lower(), resp)

        # Cleanup
        self.dev.send("s1")       # restore bit 0 active
        self.dev.send("t0")

    def run_all(self):
        print("\n=== FaultWatch Automation Test ===")
        self.test_help()
        self.test_set_error_single()
        self.test_set_error_double()
        self.test_get_error_hex()
        self.test_independence()
        self.test_input_validation()
        self.test_set_priority()
        self.test_priority_validation()
        self.test_set_error_code()
        self.test_get_diagnostic_hex()
        self.test_error_code_validation()
        self.test_code_timing()
        self.test_code_timing_validation()
        self.test_roundtrip()


# ────────────────────────────────────────────────────────────────────────────
# Checklist printer
# ────────────────────────────────────────────────────────────────────────────
def print_checklist(results: list[TestResult]):
    # Group by function name, preserve insertion order
    groups: dict[str, list[TestResult]] = {}
    for r in results:
        groups.setdefault(r.function, []).append(r)

    total_fn_pass = 0
    total_fn_fail = 0
    total_t_pass  = sum(r.passed for r in results)
    total_t_fail  = sum(not r.passed for r in results)

    width = 52
    print()
    print("=" * width)
    print(f"  {'FUNCTION CHECKLIST':^{width-4}}")
    print("=" * width)
    print(f"  {'Function':<42} {'Status':>6}")
    print("-" * width)

    for func, checks in groups.items():
        fn_pass  = sum(c.passed for c in checks)
        fn_total = len(checks)
        fn_ok    = fn_pass == fn_total

        mark   = "[OK]" if fn_ok else "[XX]"
        status = f"{fn_pass}/{fn_total}"
        print(f"  {mark} {func:<40} {status:>4}")

        if not fn_ok:
            for c in checks:
                if not c.passed:
                    print(f"       -> FAIL: {c.name}")

        if fn_ok:
            total_fn_pass += 1
        else:
            total_fn_fail += 1

    print("=" * width)
    print(f"  Functions : {total_fn_pass} OK, {total_fn_fail} failed")
    print(f"  Assertions: {total_t_pass} passed, {total_t_fail} failed")
    print("=" * width)

    if total_fn_fail == 0:
        print("\n  All library functions verified. ")
    else:
        print(f"\n  {total_fn_fail} function(s) need attention.")


# ────────────────────────────────────────────────────────────────────────────
# Entry point
# ────────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="FaultWatch serial automation test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument("port", nargs="?", default=DEFAULT_PORT,
                        help=f"Serial port (default: {DEFAULT_PORT})")
    parser.add_argument("baud", nargs="?", type=int, default=DEFAULT_BAUD,
                        help=f"Baud rate (default: {DEFAULT_BAUD})")
    args = parser.parse_args()

    tee = _open_log()
    print(f"  Log: {LOG_FILE}")

    try:
        try:
            tester = LEDTester(args.port, args.baud)
        except serial.SerialException as exc:
            print(f"\nERROR: Cannot open serial port — {exc}")
            print("  Check that the Arduino is connected and the port is correct.")
            tee.close()
            sys.exit(1)

        try:
            tester.run_all()
        finally:
            tester.close()

        print_checklist(tester.results)
        passed = all(r.passed for r in tester.results)
    finally:
        tee.close()

    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
