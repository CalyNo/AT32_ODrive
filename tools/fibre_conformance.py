#!/usr/bin/env python3
"""Fibre descriptor conformance test - needs no C toolchain.

Cross-checks the COMMITTED firmware/Inc/fibre_endpoints_generated.h against the
descriptor the generator rebuilds from firmware/Src/param.c right now, against
the official Python fibre implementation, and against the official GUI's
dashboard expectations.

Usage:  python tools/fibre_conformance.py

Exit code 0 means every check passed.  Any failure (including a missing
generator or a missing generated header) exits non-zero with a readable message
instead of an import traceback.

Scope of the evidence: `param.c` is parsed through the generator module (there is
exactly one parser in this repo, so this check catches a stale or hand-edited
header, not a wrong parser); the CRC and the descriptor handshake are checked
against the genuine ODrive host implementation; the packet server is a
line-by-line port of firmware/Src/fibre_server.c.  Nothing here runs on a board,
so USB enumeration and timing are out of scope.
"""

import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ODRIVE = Path(r"D:\1-myproject\AT32_ODrive\ODrive")
PARAM_C = ROOT / "firmware" / "Src" / "param.c"
GENERATED_H = ROOT / "firmware" / "Inc" / "fibre_endpoints_generated.h"
TUNING_DASHBOARD = ODRIVE / "GUI" / "src" / "assets" / "dashboards" / "Tuning_0_5_1.json"

# How many dashboard paths Tuning_0_5_1.json is expected to reference.  Pinned
# so that an edit to the dashboard cannot silently shrink what we verify.
EXPECTED_DASHBOARD_PATHS = 13

# Codec strings the official host understands; `float`, not `float32`
# (ODrive/tools/odrive/pyfibre ... legacy_object_client.cpp:182-194).
KNOWN_CODECS = frozenset([
    "bool", "int8", "uint8", "int16", "uint16",
    "int32", "uint32", "int64", "uint64", "float",
])

# Board identity endpoints that have no s_params[] row, so only an explicit
# table in the generator can keep them alive (design doc §4.3.1).
IDENTITY_ENDPOINTS = ("fw_version_revision",
                      "hw_version_major",
                      "hw_version_minor",
                      "hw_version_variant",
                      "serial_number")

CRC16_POLY = 0x3D65      # firmware/Src/crc.c
CRC16_INIT = 0x1337      # protocol.rst:104, non-JSON traffic
PROTOCOL_VERSION = 1     # the init value for the JSON-descriptor CRC (protocol.rst:104)

FIBRE_PROTOCOL_VERSION = 1     # firmware/Src/fibre_server.c:9
FIBRE_MAX_RESPONSE_DATA = 61   # firmware/Src/fibre_server.c:10
FIBRE_MIN_REQUEST_LENGTH = 8   # firmware/Src/fibre_server.c:11

RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, ok, detail))
    print(("  PASS  " if ok else "  FAIL  ") + name + (("   -> " + detail) if detail else ""))


def fail(message):
    """Report a blocking, non-assertion problem (missing input file, API drift)."""
    RESULTS.append((message, False, ""))
    print("  FAIL  " + message)


# ---------------------------------------------------------------------------
# Inputs
# ---------------------------------------------------------------------------

def load_generator():
    """The generator lands in Task 2.  Until then this test must fail with a
    readable message rather than an import traceback."""
    sys.path.insert(0, str(ROOT / "tools"))
    try:
        import gen_fibre_endpoints as gen
    except ImportError as exc:
        fail("tools/gen_fibre_endpoints.py is not importable yet (%s)" % exc)
        return None

    missing = [name for name in ("parse_param_rows", "build_descriptor")
               if not callable(getattr(gen, name, None))]
    if missing:
        fail("tools/gen_fibre_endpoints.py does not export %s; the generator and "
             "this test have drifted apart" % ", ".join(missing))
        return None

    return gen


def report_missing_inputs():
    absent = [p for p in (PARAM_C, GENERATED_H) if not p.is_file()]
    for path in absent:
        fail("required input missing: %s" % path)
    return not absent


def c_string_literals_to_bytes(text, symbol):
    """Extract the concatenated C string literal(s) assigned to `symbol`."""
    m = re.search(re.escape(symbol) + r"\[\]\s*=\s*(.*?);", text, re.S)
    if not m:
        raise SystemExit("FAIL: could not find %s[] in the generated header" % symbol)
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    raw = "".join(lits)
    return raw.replace('\\"', '"').replace("\\\\", "\\").encode("ascii")


# ---------------------------------------------------------------------------
# CRC: the firmware algorithm, as a literal port of firmware/Src/crc.c
# ---------------------------------------------------------------------------

def firmware_crc16(crc, data):
    """Port of crc16_update_byte() (poly 0x3D65, MSB-first, no reflection)."""
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            crc = ((crc << 1) ^ CRC16_POLY) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def load_official_protocol():
    """Import the genuine ODrive host implementation (read-only reference)."""
    pkg = ODRIVE / "tools" / "odrive" / "pyfibre"
    if not pkg.is_dir():
        fail("official pyfibre package not found at %s" % pkg)
        return None
    sys.path.insert(0, str(pkg))
    try:
        import fibre.protocol as protocol
    except Exception as exc:                                  # pragma: no cover
        fail("could not import fibre.protocol from %s (%r)" % (pkg, exc))
        return None
    return protocol


# ---------------------------------------------------------------------------
# Faithful port of fibre_server.c: fibre_server_process_packet()
# ---------------------------------------------------------------------------

class FirmwareSim:
    """Port of the AT32 firmware's Fibre packet server."""

    def __init__(self, json_bytes, scalar_reads):
        self.json = json_bytes
        self.json_crc = firmware_crc16(1, json_bytes)   # fibre_server_init(): crc16_update(1u, ...)
        self.scalar_reads = scalar_reads                # endpoint_id -> bytes it would return
        self.writes = []                                # (endpoint_id, payload) recorded
        self.sent = []                                  # responses actually emitted

    def process_packet(self, packet):
        """Returns the response bytes, or None (fibre_server.c:42-136)."""
        if packet is None or len(packet) < FIBRE_MIN_REQUEST_LENGTH:
            return None                                  # line 54-57

        seq_no = struct.unpack_from("<H", packet, 0)[0]
        endpoint_field = struct.unpack_from("<H", packet, 2)[0]
        output_length = struct.unpack_from("<H", packet, 4)[0]
        trailer = struct.unpack_from("<H", packet, len(packet) - 2)[0]

        endpoint_id = endpoint_field & 0x7FFF              # line 64
        payload = packet[6:]
        payload_length = len(packet) - FIBRE_MIN_REQUEST_LENGTH

        expected_trailer = FIBRE_PROTOCOL_VERSION if endpoint_id == 0 else self.json_crc
        if trailer != expected_trailer:
            return None                                    # line 73-77

        response = bytearray(2)
        response_length = 2                                # line 52

        if endpoint_id == 0:
            if payload_length < 4:
                return None                                # line 85-88
            offset = struct.unpack_from("<I", payload, 0)[0]
            if offset < len(self.json) and output_length > 0:
                available = len(self.json) - offset
                copy_length = min(available, output_length, FIBRE_MAX_RESPONSE_DATA)
                response[2:2 + copy_length] = self.json[offset:offset + copy_length]
                response_length = 2 + copy_length
                return self._maybe_send(endpoint_field, seq_no, response, response_length)
            return self._maybe_send(endpoint_field, seq_no, response, response_length)
        else:
            if payload_length > 0:
                self.writes.append((endpoint_id, bytes(payload)))
            if output_length > 0:
                max_length = min(output_length, FIBRE_MAX_RESPONSE_DATA)
                data = self.scalar_reads.get(endpoint_id, b"")   # default: 0 consumed
                data = data[:max_length]
                response[2:2 + len(data)] = data
                response_length = 2 + len(data)
            return self._maybe_send(endpoint_field, seq_no, response, response_length)

    def _maybe_send(self, endpoint_field, seq_no, response, response_length):
        if endpoint_field & 0x8000:                        # line 131-135
            struct.pack_into("<H", response, 0, seq_no | 0x8000)
            out = bytes(response[:response_length])
            self.sent.append(out)
            return out
        return None


# ---------------------------------------------------------------------------
# Official client side: ports of protocol.py Channel methods
# ---------------------------------------------------------------------------

class OfficialClient:
    """Ports Channel.remote_endpoint_operation + remote_endpoint_read_buffer."""

    def __init__(self, device):
        self.device = device
        self.seq = 0
        self.interface_definition_crc = 0   # set after the descriptor is read
        self.responses = {}
        self.unexpected = 0

    def remote_endpoint_operation(self, endpoint_id, input_bytes, expect_ack, output_length):
        self.seq = (self.seq + 1) & 0x7FFF            # protocol.py:272
        seq_no = self.seq | 0x80                      # protocol.py:276

        ep = endpoint_id | (0x8000 if expect_ack else 0)
        packet = struct.pack("<HHH", seq_no, ep, output_length) + input_bytes   # :277-278

        if (endpoint_id & 0x7FFF) == 0:               # protocol.py:281-284
            trailer = PROTOCOL_VERSION
        else:
            trailer = self.interface_definition_crc
        packet += struct.pack("<H", trailer)          # :286

        reply = self.device.process_packet(packet)
        if expect_ack:
            if reply is None:
                return None                           # resend / timeout in the real client
            got = struct.unpack_from("<H", reply, 0)[0]   # process_packet():343
            if not (got & 0x8000):
                self.unexpected += 1
                return None
            # protocol.py keys _expected_acks by the FULL seq_no (with the 0x80
            # bit), and process_packet() masks the received value to 0x7fff.
            if (got & 0x7FFF) != (self.seq | 0x80):
                self.unexpected += 1
                return None
            return reply[2:]                          # protocol.py:349
        return None

    def remote_endpoint_read_buffer(self, endpoint_id):
        """protocol.py:323-335, chunk_length = 512."""
        buf = b""
        while True:
            chunk = self.remote_endpoint_operation(
                endpoint_id, struct.pack("<I", len(buf)), True, 512)
            if chunk is None:
                return None
            if len(chunk) == 0:
                break
            buf += chunk
        return buf


# ---------------------------------------------------------------------------
# Descriptor inspection helpers
# ---------------------------------------------------------------------------

def flatten_tree(top):
    """Split a descriptor tree into object paths and leaf members.

    A leaf is either a property (with a codec `type` and an `id`) or a function
    (`type` == "function", no codec).  Both are addressable by the host, but only
    properties have a codec to validate.
    """
    objects, props, funcs = [], [], []

    def walk(nodes, prefix):
        for entry in nodes:
            name = entry.get("name")
            path = name if not prefix else prefix + "." + name
            if entry.get("type") == "object":
                objects.append(path)
                walk(entry.get("members", []), path)
            elif entry.get("type") == "function":
                funcs.append(path)
            else:
                props.append(path)

    if isinstance(top, list):
        walk(top, "")
    return objects, props, funcs


def iter_members(top):
    """Yield every member entry in the tree, objects included."""
    def walk(nodes):
        for entry in nodes:
            yield entry
            if entry.get("type") == "object":
                for child in walk(entry.get("members", [])):
                    yield child

    if isinstance(top, list):
        for entry in walk(top):
            yield entry


def main():
    print("=" * 78)
    print("AT32_ODrive Fibre descriptor conformance")
    print("=" * 78)
    print("firmware : %s" % ROOT)
    print("official : %s" % ODRIVE)
    print()

    if not ODRIVE.is_dir():
        raise SystemExit("FAIL: official ODrive reference clone not found at %s "
                         "(read-only reference; do not modify)" % ODRIVE)

    # ------------------------------------------------------------------
    print("[1] Inputs")
    gen = load_generator()
    report_missing_inputs()
    if gen is None or not all(p.is_file() for p in (PARAM_C, GENERATED_H)):
        print()
        print("      Expected at this stage: the generator and the generated header "
              "are created by later tasks.")
        return finish()

    # ------------------------------------------------------------------
    print()
    print("[2] CRC-16 agreement with the official implementation")

    protocol = load_official_protocol()
    if protocol is None:
        return finish()

    # docs/protocol.rst:106-108 vectors, initial value 0x1337.
    ok = True
    for data, expect in ((b"\x01\x02\x03\x04", 0x672E), (b"\x05\x04\x03\x02\x01", 0xE251)):
        fw = firmware_crc16(CRC16_INIT, data)
        official = protocol.calc_crc16(CRC16_INIT, data)
        good = (fw == expect == official)
        ok = ok and good
        check("protocol.rst vector %s -> 0x%04X" % (data.hex(), expect), good,
              "firmware port=0x%04X official=0x%04X" % (fw, official))

    probe = bytes(range(0, 64))
    fw = firmware_crc16(CRC16_INIT, probe)
    official = protocol.calc_crc16(CRC16_INIT, probe)
    check("firmware port == official calc_crc16 over 0x00..0x3F", fw == official,
          "firmware=0x%04X official=0x%04X" % (fw, official))
    if not ok:
        print()
        print("      The CRC used for the descriptor trailer is wrong; every host "
              "request would be dropped.")

    # ------------------------------------------------------------------
    print()
    print("[3] Descriptor bytes: independent re-parse vs the committed header")

    param_text = PARAM_C.read_text(encoding="utf-8")
    rows = gen.parse_param_rows(param_text)
    built = gen.build_descriptor(rows)
    # The generator returns a tuple whose later elements are unused here; only
    # the descriptor bytes are compared, so the rest of the contract is free.
    expected = built[0] if isinstance(built, tuple) else built
    if isinstance(expected, str):
        expected = expected.encode("ascii")

    header_text = GENERATED_H.read_text(encoding="utf-8")
    committed = c_string_literals_to_bytes(header_text, "s_fibre_json")

    print("        s_params[] rows      : %d" % len(rows))
    print("        expected descriptor  : %d bytes" % len(expected))
    print("        committed descriptor : %d bytes" % len(committed))

    same = committed == expected
    check("s_fibre_json matches the re-parse of param.c byte for byte", same,
          "" if same else describe_first_difference(expected, committed))

    # ------------------------------------------------------------------
    print()
    print("[4] Descriptor structure")

    try:
        top = json.loads(committed)
    except Exception as exc:
        check("descriptor is valid JSON", False, repr(exc))
        return finish()
    check("descriptor is valid JSON", True)
    check("descriptor is pure ASCII with no NUL byte",
          all(b < 0x80 for b in committed) and b"\x00" not in committed,
          "the device serves sizeof(s_fibre_json) - 1 bytes")

    check("top level is a list", isinstance(top, list),
          "got %s" % type(top).__name__)
    if not isinstance(top, list):
        return finish()

    objects, paths, functions = flatten_tree(top)
    leaves = paths + functions
    ids = [e["id"] for e in iter_members(top) if "id" in e]
    check("root is not empty", len(top) > 0,
          "%d root members, %d properties, %d functions, %d objects"
          % (len(top), len(paths), len(functions), len(objects)))

    duplicates = sorted({i for i in ids if ids.count(i) > 1})
    contiguous = sorted(ids) == list(range(1, len(ids) + 1))
    check("endpoint ids are unique", not duplicates,
          ("duplicated: %s" % duplicates) if duplicates else "%d ids" % len(ids))
    check("endpoint ids are 1..N with no gaps", contiguous,
          "min=%s max=%s count=%d" % (min(ids) if ids else "-", max(ids) if ids else "-", len(ids)))

    bad_names = [e.get("name") for e in iter_members(top) if not e.get("name")]
    check("every member has a name", not bad_names, "%r" % (bad_names[:4],))

    # Every *property* codec must be one the official host accepts.
    codecs = {}

    def collect_codecs(nodes):
        for entry in nodes:
            if entry.get("type") == "object":
                collect_codecs(entry.get("members", []))
            elif entry.get("type") != "function":
                codecs.setdefault(entry.get("type"), []).append(entry.get("name"))

    collect_codecs(top)
    unknown = sorted(set(codecs) - KNOWN_CODECS)
    check("every property codec is in the official whitelist", not unknown,
          ("unknown: %s" % unknown) if unknown else "used: %s" % ", ".join(sorted(codecs)))

    missing_identity = [name for name in IDENTITY_ENDPOINTS
                        if name not in [p.split(".")[-1] for p in leaves]]
    check("identity endpoints survive the param.c-driven generation", not missing_identity,
          ("missing: %s" % missing_identity) if missing_identity
          else ", ".join(IDENTITY_ENDPOINTS))

    check("fw_version_major / fw_version_minor present",
          "fw_version_major" in leaves and "fw_version_minor" in leaves)

    # ------------------------------------------------------------------
    print()
    print("[5] Descriptor CRC")

    json_crc = firmware_crc16(PROTOCOL_VERSION, committed)
    official_crc = protocol.calc_crc16(PROTOCOL_VERSION, committed)
    print("        json_crc(poly 0x3D65, init 1) = 0x%04X" % json_crc)
    check("firmware port == official calc_crc16 for the descriptor", json_crc == official_crc,
          "firmware=0x%04X official=0x%04X" % (json_crc, official_crc))

    m = re.search(r"json\s+crc\s*:\s*0x([0-9A-Fa-f]+)", header_text)
    check("generated header records the same json_crc", m is not None and int(m.group(1), 16) == json_crc,
          ("header 0x%04X vs computed 0x%04X" % (int(m.group(1), 16), json_crc)) if m
          else "no 'json crc : 0x....' line in the generated header")

    # ------------------------------------------------------------------
    print()
    print("[6] Descriptor read handshake (official read loop vs firmware packet server)")

    device = FirmwareSim(committed, scalar_reads={})
    client = OfficialClient(device)

    got = client.remote_endpoint_read_buffer(0)
    check("descriptor retrieved complete", got == committed,
          "got %s bytes" % (len(got) if got is not None else "None"))

    client.interface_definition_crc = protocol.calc_crc16(PROTOCOL_VERSION, got or b"")
    check("client json_crc == the firmware's expected trailer",
          client.interface_definition_crc == device.json_crc,
          "client=0x%04X firmware=0x%04X" % (client.interface_definition_crc, device.json_crc))
    check("no unexpected ACK along the way", client.unexpected == 0,
          "%d unexpected" % client.unexpected)
    check("every response stayed within the 61-byte clamp",
          all(len(r) - 2 <= FIBRE_MAX_RESPONSE_DATA for r in device.sent),
          "%d round trips" % len(device.sent))

    # A host whose descriptor hashed differently must be ignored, not answered
    # with a tree it does not agree on.
    bogus = OfficialClient(FirmwareSim(committed, scalar_reads={}))
    bogus.interface_definition_crc = (device.json_crc ^ 0x0001) & 0xFFFF
    check("request with a mismatched json_crc is silently dropped",
          bogus.remote_endpoint_operation(1, b"", True, 4) is None)

    # ------------------------------------------------------------------
    print()
    print("[7] Official GUI Tuning dashboard paths")

    if not TUNING_DASHBOARD.is_file():
        check("Tuning_0_5_1.json found", False, str(TUNING_DASHBOARD))
    else:
        wanted = re.findall(r'"path"\s*:\s*"odrives\.odrive0\.([^"]+)"',
                            TUNING_DASHBOARD.read_text(encoding="utf-8"))
        check("dashboard declares the expected number of paths",
              len(wanted) == EXPECTED_DASHBOARD_PATHS,
              "%d paths (pinned at %d)" % (len(wanted), EXPECTED_DASHBOARD_PATHS))

        flat = set(leaves)
        wanted_unique = {p.split(",")[0].strip() for p in wanted}
        missing = sorted(wanted_unique - flat)
        for path in missing:
            print("        MISSING in the endpoint tree : %s" % path)
        check("every dashboard path resolves in the endpoint tree", not missing,
              "%d/%d distinct paths resolved (%d path declarations in the dashboard)"
              % (len(wanted_unique) - len(missing), len(wanted_unique), len(wanted)))

    return finish()


def describe_first_difference(expected, committed):
    limit = min(len(expected), len(committed))
    at = next((i for i in range(limit) if expected[i] != committed[i]), limit)
    context = max(0, at - 30)
    return ("first difference at byte %d: expected %r got %r"
            % (at, expected[context:at + 30], committed[context:at + 30]))


def finish():
    print()
    print("=" * 78)
    failed = [r for r in RESULTS if not r[1]]
    print("RESULT: %d checks, %d passed, %d failed" %
          (len(RESULTS), len(RESULTS) - len(failed), len(failed)))
    for name, _, detail in failed:
        print("  FAILED: %s  %s" % (name, detail))
    print("=" * 78)
    print("NOTE: host-side simulation only - no board, no USB stack, no timing.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
