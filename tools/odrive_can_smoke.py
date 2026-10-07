#!/usr/bin/env python3
"""Smoke test for the ODrive CAN Simple subset implemented by this firmware.

Uses the official ODrive DBC (tools/odrive-cansimple.dbc from the ODrive tree)
plus python-can, so it talks to the board exactly like any other ODrive CAN
host does: node id in bits 5..10, command in bits 0..4, heartbeat decoded as
Axis_Error / Axis_State / the three error flags / Trajectory_Done_Flag.

STATUS: not yet run against hardware. It is a bring-up aid, not a verified
test - treat any mismatch as a finding, not as a bug in the script.

Requires: pip install python-can cantools
Example:
    python tools/odrive_can_smoke.py --interface canalystii --channel 0 --bitrate 250000
    python tools/odrive_can_smoke.py --interface slcan --channel COM7 --bitrate 250000
    python tools/odrive_can_smoke.py --interface socketcan --channel can0 --bitrate 250000
"""
from __future__ import annotations

import argparse
import os
import sys
import time

try:
    import can
    import cantools
except ImportError:  # pragma: no cover
    sys.exit("python-can and cantools are required: pip install python-can cantools")

CMD_HEARTBEAT = 0x01
CMD_SET_AXIS_STATE = 0x07
CMD_GET_ENCODER_ESTIMATES = 0x09
CMD_GET_ENCODER_COUNT = 0x0A
CMD_GET_BUS_VOLTAGE_CURRENT = 0x17
CMD_CLEAR_ERRORS = 0x18

DBC_CANDIDATES = (
    os.environ.get("ODRIVE_DBC", ""),
    os.path.join(os.path.dirname(__file__), "odrive-cansimple.dbc"),
)


def load_dbc(explicit: str | None):
    candidates = ([explicit] if explicit else []) + list(DBC_CANDIDATES)
    for path in candidates:
        if path and os.path.isfile(path):
            db = cantools.database.load_file(path)
            print(f"DBC: {path} ({len(db.messages)} messages)")
            return db
    sys.exit("could not find odrive-cansimple.dbc; pass --dbc PATH")


def msg_id(node_id: int, cmd: int) -> int:
    return (node_id << 5) | cmd


def decode(db, node_id: int, cmd: int, data: bytes):
    """Decode a received frame with the DBC, or return None if it is not mapped."""
    try:
        message = db.get_message_by_frame_id(msg_id(node_id, cmd))
    except KeyError:
        return None
    return message.decode(data)


def request_rtr(bus, node_id: int, cmd: int, timeout: float = 1.0):
    """Send a remote frame and return the first matching reply."""
    bus.send(can.Message(arbitration_id=msg_id(node_id, cmd), is_extended_id=False,
                         is_remote_frame=True, dlc=8))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frame = bus.recv(timeout=0.1)
        if frame is not None and frame.arbitration_id == msg_id(node_id, cmd):
            return frame.data
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--interface", required=True,
                    help="python-can interface: canalystii / slcan / gs_usb / socketcan / pcan ...")
    ap.add_argument("--channel", required=True, help="channel / port / bitrate-dependent value")
    ap.add_argument("--bitrate", type=int, default=250000,
                    help="must match can.config.baud_rate (default 250000, as ODrive ships)")
    ap.add_argument("--node-id", type=int, default=0)
    ap.add_argument("--dbc", default=None)
    ap.add_argument("--set-state", type=int, default=None,
                    help="request AXIS_STATE_* (1=IDLE, 3=FULL_CAL, 8=CLOSED_LOOP)")
    ap.add_argument("--clear-errors", action="store_true")
    args = ap.parse_args()

    db = load_dbc(args.dbc)
    bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)
    failures = 0

    def check(name: str, ok: bool, detail: str = "") -> None:
        nonlocal failures
        print(f"{'PASS' if ok else 'FAIL'} {name}{(' - ' + detail) if detail else ''}")
        if not ok:
            failures += 1

    try:
        # --- cyclic heartbeat (no request needed) ---------------------------
        print(f"waiting for heartbeat 0x{msg_id(args.node_id, CMD_HEARTBEAT):03X} ...")
        heartbeat = None
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            frame = bus.recv(timeout=0.2)
            if frame is not None and frame.arbitration_id == msg_id(args.node_id, CMD_HEARTBEAT):
                heartbeat = decode(db, args.node_id, CMD_HEARTBEAT, bytes(frame.data))
                break

        if heartbeat is None:
            check("heartbeat-received", False,
                  "no heartbeat within 3 s - check bitrate, wiring, 120R termination, node id")
        else:
            check("heartbeat-received", True)
            for key, value in heartbeat.items():
                print(f"    {key}: {value}")

        # --- request/response: encoder estimates via RTR --------------------
        data = request_rtr(bus, args.node_id, CMD_GET_ENCODER_ESTIMATES)
        if data is None:
            check("rtr-encoder-estimates", False, "no reply to the remote frame")
        else:
            decoded = decode(db, args.node_id, CMD_GET_ENCODER_ESTIMATES, bytes(data))
            check("rtr-encoder-estimates", True, str(decoded))

        data = request_rtr(bus, args.node_id, CMD_GET_BUS_VOLTAGE_CURRENT)
        if data is None:
            check("rtr-bus-voltage", False, "no reply to the remote frame")
        else:
            decoded = decode(db, args.node_id, CMD_GET_BUS_VOLTAGE_CURRENT, bytes(data))
            check("rtr-bus-voltage", True, str(decoded))

        # --- optional actions ----------------------------------------------
        if args.clear_errors:
            bus.send(can.Message(arbitration_id=msg_id(args.node_id, CMD_CLEAR_ERRORS),
                                 is_extended_id=False, data=b"\x00"))
            print("sent CLEAR_ERRORS")

        if args.set_state is not None:
            bus.send(can.Message(arbitration_id=msg_id(args.node_id, CMD_SET_AXIS_STATE),
                                 is_extended_id=False,
                                 data=bytes([args.set_state, 0, 0, 0, 0, 0, 0, 0])))
            print(f"requested axis state {args.set_state}")
            deadline = time.monotonic() + 3.0
            while time.monotonic() < deadline:
                frame = bus.recv(timeout=0.2)
                if frame is not None and frame.arbitration_id == msg_id(args.node_id, CMD_HEARTBEAT):
                    hb = decode(db, args.node_id, CMD_HEARTBEAT, bytes(frame.data))
                    print(f"    -> Axis_State={hb['Axis_State']} Axis_Error=0x{hb['Axis_Error']:08X}")
                    break
    finally:
        bus.shutdown()

    print(f"\n{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
