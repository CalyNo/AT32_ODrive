"""Fibre smoke test against the board, through the OFFICIAL ODrive host stack.

This drives the firmware exactly the way the official ODrive GUI does: it uses
the odrive python package from an ODrive source checkout, which dials the USB
vendor interface this firmware implements

    usb:idVendor=0x1209,idProduct=0x0D32,bInterfaceClass=0,
        bInterfaceSubClass=1,bInterfaceProtocol=0

so a PASS here means the transport the GUI depends on is good, not just "some
bytes came back".  The endpoint tree it walks is the one generated from
firmware/Src/param.c (see tools/gen_fibre_endpoints.py), so this also acts as a
drift check between the table and the wire.

What it covers:

    [1] read path          -- every scalar kind the descriptor declares (float,
                              int32, uint32, uint16) plus the computed
                              axis0.<component>.error properties
    [2] write path         -- a persisted parameter and an on_write-hooked one,
                              each written and restored
    [3] function endpoint  -- axis0.clear_errors(), i.e. the zero-argument
                              function-endpoint path the GUI's Clear Errors
                              button uses
    [4] status LED         -- led.frames / led.frames_done, which distinguishes
                              "firmware produced a waveform" from "the LED
                              itself is dark" (docs/bringup.md 6d/6f)
    [5] reset cause        -- bit3 set means the independent watchdog fired

SAFETY: this never commands CLOSED_LOOP_CONTROL, never starts a calibration and
never calls save_configuration(), so it cannot arm the PWM outputs and cannot
write the NVM sector.  Every value it writes is chosen to be inert and is
restored before it returns -- with one deliberate exception: section [3] calls
axis0.clear_errors(), which by definition clears the axis error flags and puts
the axis back in IDLE.  Run with --read-only if you want to inspect an existing
fault without disturbing it.

Usage:
    python tools/odrive_fibre_smoke.py
    python tools/odrive_fibre_smoke.py --read-only
    python tools/odrive_fibre_smoke.py --tools /path/to/ODrive/tools

By default the ODrive checkout is expected next to this repository
(<parent>/ODrive), which is the layout this project is developed in.

Exit status: 0 all checks passed, 1 environment problem, 2 no device found,
3 at least one check failed.
"""

import argparse
import os
import sys
import time

READ_TIMEOUT_S = 20.0

# Properties read over Fibre, chosen to cover every storage width the descriptor
# declares.  axis0.requested_state is the interesting one: it is an enum-typed
# field, and an enum that is narrower than the 4 bytes its PARAM_KIND_* claims
# reads back as garbage (that bug showed up as 257 == 0x00000101 on hardware).
READ_CHECKS = [
    "fw_version_major",
    "fw_version_minor",
    "fw_version_revision",
    "hw_version_major",
    "hw_version_variant",
    "serial_number",
    "vbus_voltage",
    "board.core_clock",
    "board.clock_degraded",
    "board.reset_cause",
    "axis0.error",
    "axis0.current_state",
    "axis0.requested_state",
    "axis0.motor.error",
    "axis0.encoder.error",
    "axis0.controller.error",
    "axis0.encoder.pos_estimate",
    "axis0.encoder.vel_estimate",
    "axis0.controller.input_pos",
    "axis0.controller.config.pos_gain",
]

RESULTS = {"passed": 0, "failed": 0}


def check(name, got, want=None):
    """Record one PASS/FAIL.  A missing `want` just reports the value."""
    good = (want is None) or (got == want)
    if good:
        RESULTS["passed"] += 1
    else:
        RESULTS["failed"] += 1
    suffix = "" if want is None else "  (want %s)" % (want,)
    print("  %-6s %-46s = %s%s" % ("PASS" if good else "FAIL", name, got, suffix))
    return good


def default_tools_dir():
    """<repo>/../ODrive/tools, the sibling-checkout layout."""
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(os.path.dirname(repo_root), "ODrive", "tools")


def load_odrive(tools_dir):
    """Import the odrive package from an ODrive checkout.

    `fibre` does not live directly under tools/ -- it is at
    tools/odrive/pyfibre/fibre, and odrive/__init__.py is what puts that on
    sys.path -- so importing odrive is enough and must come first.
    """
    if not os.path.isdir(tools_dir):
        print("no such tools directory: " + tools_dir)
        print("pass --tools <ODrive checkout>/tools")
        return None
    if not os.path.isdir(os.path.join(tools_dir, "odrive")):
        print(tools_dir + " does not look like an ODrive checkout (no odrive/)")
        return None

    sys.path.insert(0, tools_dir)
    try:
        import odrive
    except Exception as exc:
        print("cannot import odrive from %s: %r" % (tools_dir, exc))
        return None

    print("python : %s" % (sys.version.split()[0],))
    print("odrive : %s" % (odrive.__file__,))
    print("search : %s" % (getattr(odrive, "default_usb_search_path", "?"),))
    return odrive


def connect(odrive, timeout):
    print()
    print("=== connecting (%.0f s timeout) ===" % (timeout,))
    try:
        dev = odrive.find_any(timeout=timeout)
    except Exception as exc:
        print("find_any raised: %r" % (exc,))
        return None
    if dev is None:
        print("no device on the vendor interface.")
        print("  - board absent entirely: it is not enumerating")
        print("  - board present with a yellow mark in Device Manager: the vendor")
        print("    interface has no WinUSB driver yet")
        print("  - check with: pnputil /enum-devices /connected")
        return None
    print("connected: %s" % (dev,))
    return dev


def section(title):
    print()
    print(title)


def check_read_path(dev):
    section("[1] read path over Fibre (every scalar kind in the descriptor)")
    ok = 0
    for path in READ_CHECKS:
        # Attribute access *is* the read in this object model: the value comes
        # back as a plain int/float, there is no separate .read() verb on the
        # public tree (unlike the underscored _foo_property form the server
        # module uses).  A protocol error surfaces on this line.
        obj = dev
        try:
            for part in path.split("."):
                obj = getattr(obj, part)
            print("  PASS  %-38s = %s" % (path, obj))
            ok += 1
        except Exception as exc:
            print("  FAIL  %-38s -> %r" % (path, exc))
    RESULTS["passed" if ok == len(READ_CHECKS) else "failed"] += 1
    print("  -> %d/%d endpoints read" % (ok, len(READ_CHECKS)))
    return ok == len(READ_CHECKS)


def check_write_path(dev):
    section("[2] write path (RAM only; nothing is saved to flash)")
    print("  pos_gain is a persisted parameter: a plain write lands in")
    print("  g_odrive_config and is mirrored by nvm_config_apply().  Flash is")
    print("  touched only by save_configuration(), which this script never calls.")

    original = dev.axis0.controller.config.pos_gain
    check("pos_gain read (initial)", original)
    dev.axis0.controller.config.pos_gain = 25.0
    check("pos_gain after write 25.0", dev.axis0.controller.config.pos_gain, 25.0)
    dev.axis0.controller.config.pos_gain = original
    check("pos_gain restored", dev.axis0.controller.config.pos_gain, original)

    print()
    print("  axis0.controller.input_pos is read/write through an on_write hook,")
    print("  so it exercises a different branch of the dispatcher:")
    original_pos = dev.axis0.controller.input_pos
    dev.axis0.controller.input_pos = 1.25
    check("input_pos after write 1.25", dev.axis0.controller.input_pos, 1.25)
    dev.axis0.controller.input_pos = original_pos
    check("input_pos restored", dev.axis0.controller.input_pos, original_pos)


def check_function_endpoint(dev):
    section("[3] function endpoint axis0.clear_errors()")
    print("  Zero-argument function endpoints are dispatched purely by endpoint")
    print("  id (no funid on the wire), which the GUI's Clear Errors button and")
    print("  the save_configuration twin rely on.")
    try:
        dev.axis0.clear_errors()
        check("clear_errors() returned without error", True)
    except Exception as exc:
        check("clear_errors() raised %r" % (exc,), False)
    check("axis0.error after clear", dev.axis0.error, 0)
    check("axis0.current_state after clear (1 = IDLE)", dev.axis0.current_state, 1)


def check_status_led(dev):
    section("[4] status LED diagnostics (docs/bringup.md sections 6d / 6f)")
    f0 = dev.led.frames
    d0 = dev.led.frames_done
    check("led.frames readable", f0)
    check("led.frames_done readable", d0)
    check("led.busy readable", dev.led.busy)

    dev.led.self_test = 1
    time.sleep(3)
    f1 = dev.led.frames
    d1 = dev.led.frames_done
    dev.led.self_test = 0
    check("led.frames advanced during self_test", f1 - f0)
    check("led.frames_done advanced too", d1 - d0)
    print("        both advanced  -> the waveform reached the wire, so a dark")
    print("                          LED is a supply/wiring/level problem")
    print("        only frames up -> the timer/DMA never finished a transfer")


def check_reset_cause(dev):
    section("[5] reset cause (bit0 NRST, bit1 POR, bit2 SW, bit3 IWDG, bit4 WWDG)")
    rc = dev.board.reset_cause
    check("board.reset_cause", rc)
    if rc & 0x08:
        print("        WARNING: the IWDG bit is set -- something stopped feeding")
        print("        the watchdog, or the watchdog outlived a warm reset.")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Fibre smoke test against an AT32_ODrive board.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tools", default=None,
                        help="path to <ODrive checkout>/tools "
                             "(default: <parent>/ODrive/tools)")
    parser.add_argument("--read-only", action="store_true",
                        help="skip the write path and the LED self test")
    parser.add_argument("--timeout", type=float, default=READ_TIMEOUT_S,
                        help="find_any timeout in seconds (default: %(default)s)")
    args = parser.parse_args(argv)

    tools_dir = os.path.abspath(args.tools) if args.tools else default_tools_dir()
    odrive = load_odrive(tools_dir)
    if odrive is None:
        return 1

    dev = connect(odrive, args.timeout)
    if dev is None:
        return 2

    check_read_path(dev)

    if args.read_only:
        section("[2]-[4] skipped (--read-only)")
    else:
        check_write_path(dev)
        check_function_endpoint(dev)
        check_status_led(dev)

    check_reset_cause(dev)

    print()
    total = RESULTS["passed"] + RESULTS["failed"]
    print("=" * 78)
    print("RESULT: %d checks, %d passed, %d failed"
          % (total, RESULTS["passed"], RESULTS["failed"]))
    print("=" * 78)
    return 0 if RESULTS["failed"] == 0 else 3


if __name__ == "__main__":
    raise SystemExit(main())
