#!/usr/bin/env python3
"""Smoke test for the ODrive-compatible ASCII protocol over UART.

Exercises the exact response semantics a stock ODrive host relies on:
  * "r <path>" answers with the bare value (no "path: " prefix)
  * "w ..." answers nothing on success
  * "f <motor>" answers "pos vel"
  * p / v / c / t are accepted

STATUS: not yet run against hardware. It is a bring-up aid, not a verified
test - treat any mismatch as a finding, not as a bug in the script.

Requires pyserial. Example:
    python tools/odrive_ascii_smoke.py --port COM3
    python tools/odrive_ascii_smoke.py --port COM3 --closed-loop   # enables PWM!
"""
from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required: pip install pyserial")

AXIS_STATE_IDLE = 1
AXIS_STATE_CLOSED_LOOP_CONTROL = 8


class ODriveAscii:
    def __init__(self, port: str, baud: int, timeout: float = 1.0) -> None:
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=timeout)

    def close(self) -> None:
        self.ser.close()

    def drain(self) -> None:
        self.ser.reset_input_buffer()

    def command(self, line: str, expect_reply: bool = True, timeout: float = 1.0) -> str:
        """Send one ASCII line and return the reply (empty for silent commands)."""
        self.ser.write((line + "\n").encode("ascii"))
        self.ser.flush()
        if not expect_reply:
            return ""
        deadline = time.monotonic() + timeout
        buf = b""
        while time.monotonic() < deadline:
            chunk = self.ser.read(1)
            if not chunk:
                continue
            if chunk == b"\n":
                break
            buf += chunk
        return buf.decode("ascii", "replace").strip()

    def read_float(self, path: str) -> float:
        reply = self.command(f"r {path}")
        return float(reply)  # raises ValueError on "invalid property"

    def read_int(self, path: str) -> int:
        reply = self.command(f"r {path}")
        return int(float(reply))

    def write(self, path: str, value) -> str:
        return self.command(f"w {path} {value}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port, e.g. COM3 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--axis", type=int, default=0)
    ap.add_argument("--closed-loop", action="store_true",
                    help="DANGEROUS: request CLOSED_LOOP_CONTROL (PWM enabled)")
    ap.add_argument("--save", action="store_true", help="run 'ss' at the end")
    args = ap.parse_args()

    if args.axis != 0:
        print("only axis 0 exists on this firmware")
        return 2

    dev = ODriveAscii(args.port, args.baud)
    failures = 0

    def check(name: str, ok: bool, detail: str = "") -> None:
        nonlocal failures
        print(f"{'PASS' if ok else 'FAIL'} {name}{(' - ' + detail) if detail else ''}")
        if not ok:
            failures += 1

    try:
        dev.drain()
        time.sleep(0.2)

        # --- read semantics: bare value, no path prefix ---------------------
        try:
            vbus = dev.read_float("vbus_voltage")
            check("read-float-parses", True, f"vbus_voltage={vbus:.3f} V")
            check("read-float-plausible", 0.0 < vbus < 60.0, "expect 8-24 V bus")
        except ValueError as exc:
            check("read-float-parses", False, f"reply was not a bare number ({exc})")

        try:
            state = dev.read_int("axis0.current_state")
            check("read-int-parses", True, f"current_state={state}")
        except ValueError as exc:
            check("read-int-parses", False, f"reply was not a bare number ({exc})")

        try:
            pos, vel = (float(x) for x in dev.command("f 0").split())
            check("feedback-parses", True, f"pos={pos:.4f} rev vel={vel:.4f} rev/s")
        except ValueError as exc:
            check("feedback-parses", False, f"'f 0' reply unparseable ({exc})")

        # --- unknown property / read-only property --------------------------
        check("unknown-property",
              dev.command("r no.such.path") == "invalid property")
        check("read-only-property",
              dev.command("r axis0.clear_errors") == "not implemented")

        # --- write semantics: silent on success -----------------------------
        noise = dev.command("w axis0.controller.input_pos 0", expect_reply=False)
        time.sleep(0.2)
        check("write-is-silent", dev.ser.in_waiting == 0, f"{noise!r}")

        # --- invalid write is reported --------------------------------------
        reply = dev.command("w axis0.motor.config.pole_pairs 0")
        check("write-rejects-bad-value", reply == "invalid value", reply)
        reply = dev.command("w no.such.path 1")
        check("write-unknown-property", reply == "invalid property", reply)

        # --- optional: enable closed loop -----------------------------------
        if args.closed_loop:
            dev.write("axis0.requested_state", AXIS_STATE_CLOSED_LOOP_CONTROL)
            time.sleep(0.2)
            state = dev.read_int("axis0.current_state")
            err = dev.read_int("axis0.error")
            check("closed-loop-entered", state == AXIS_STATE_CLOSED_LOOP_CONTROL,
                  f"state={state} error=0x{err:08X}")
            dev.write("axis0.requested_state", AXIS_STATE_IDLE)
            time.sleep(0.2)
            check("idle-restored", dev.read_int("axis0.current_state") == AXIS_STATE_IDLE)

        if args.save:
            dev.command("ss", expect_reply=False)
            time.sleep(0.5)
            check("save-is-silent", dev.ser.in_waiting == 0)
    finally:
        dev.close()

    print(f"\n{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
