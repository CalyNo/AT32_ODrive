#!/usr/bin/env python3
"""Fibre descriptor conformance test - needs no C toolchain.

Cross-checks the COMMITTED firmware/Inc/fibre_endpoints_generated.h against the
descriptor the generator rebuilds from firmware/Src/param.c right now, against
the official Python fibre implementation, and against the official GUI's
dashboard expectations.

Usage:  python tools/fibre_conformance.py

Exit code 0 means every check passed.  Any failure (including a missing
generator or a missing generated header) exits non-zero with a readable message
instead of an import traceback (SystemExit is used for blocking input problems).

Printed sections are numbered to match the implementation plan's requirement
numbers, so output maps 1:1 onto the plan:

  [inputs]                        generator + generated header present
  [1] independent re-parse of param.c, byte-for-byte equal to the committed
      s_fibre_json[] (requirement 1 + 3)
  [3] CRC-16 three-way agreement: official module, protocol.rst vectors, the
      firmware algorithm ported from crc.c (requirement 5)
  [4] descriptor structure: valid JSON, top-level list, ids unique and 1..N,
      codec whitelist, identity endpoints present at the root (requirement 4)
  [6] descriptor read handshake: firmware packet server port driven by the
      official client's read loop (requirement 6)
  [7] official GUI Tuning dashboard paths all resolve (requirement 7)
  [extra] checks that are not among the seven requirements

Requirement 2 is "the test itself", so it has no runtime section of its own;
"not covered" below is where it would be noted.

Scope of the evidence: `param.c` is parsed through the generator module (there is
exactly one parser in this repo, so this check catches a stale or hand-edited
header, not a wrong parser); the CRC and the descriptor handshake are checked
against the genuine ODrive host implementation; the packet server is a
line-by-line port of firmware/Src/fibre_server.c.  Nothing here runs on a board,
so USB enumeration and timing are out of scope.

The simulator below is a faithful port of the CURRENT fibre_server.c: it offers
the payload to the endpoint unconditionally (the pre-Task-5 `payload_length > 0`
gate silently skipped every zero-argument function call, so save_configuration()
never ran) and it implements the endpoint-0 0xFFFFFFFF version-id sentinel.

What the port still *models* rather than verifies is the decision inside
fibre_endpoint_write() (fibre_endpoints.c): the simulator derives the
property/function/identity kind and the codec width from the descriptor's own
declaration -- the same declarations the generated s_fibre_ep_kind[] and
s_fibre_ep_width[] tables are built from -- so it is a model of that layer, not
evidence about the C in it.  Only the packet framing, the unconditional dispatch
and the sentinel are properties of fibre_server.c itself.
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

# Wire width of each codec, in bytes.  A property's response must be exactly this
# long: the host parses the tree from the declared codec, so a short or long
# answer shifts every following field.
CODEC_WIDTH = {"bool": 1, "int8": 1, "uint8": 1, "int16": 2, "uint16": 2,
               "int32": 4, "uint32": 4, "int64": 8, "uint64": 8, "float": 4}

# Board identity endpoints that have no s_params[] row, so only an explicit
# table in the generator can keep them alive (design doc §4.3.1).  They must sit
# at the ROOT of the tree, exactly as the official descriptor has them.
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
    """Report a blocking problem.  Returns None, so callers must stop the section."""
    RESULTS.append((message, False, ""))
    print("  FAIL  " + message)
    return None


# ---------------------------------------------------------------------------
# Inputs
# ---------------------------------------------------------------------------

def load_generator():
    """Import the generator (created in Task 2) and pin the API this test needs.

    A missing generator, or one that does not expose the two functions agreed
    with this test, is a blocking condition: raise SystemExit so `main()` reports
    it as a failure instead of letting an ImportError traceback escape.
    """
    sys.path.insert(0, str(ROOT / "tools"))
    try:
        import gen_fibre_endpoints as gen
    except ImportError as exc:
        raise SystemExit("tools/gen_fibre_endpoints.py is not importable yet (%s)" % exc)

    missing = [name for name in ("parse_param_rows", "build_descriptor")
               if not callable(getattr(gen, name, None))]
    if missing:
        raise SystemExit("tools/gen_fibre_endpoints.py does not export %s; the "
                         "generator and this test have drifted apart" % ", ".join(missing))

    return gen


def report_missing_inputs():
    absent = [p for p in (PARAM_C, GENERATED_H) if not p.is_file()]
    for path in absent:
        fail("required input missing: %s" % path)
    return None if absent else True


def c_string_literals_to_bytes(text, symbol):
    """Extract the concatenated C string literal(s) assigned to `symbol`."""
    m = re.search(re.escape(symbol) + r"\[\]\s*=\s*(.*?);", text, re.S)
    if not m:
        raise SystemExit("FAIL: could not find %s[] in the generated header" % symbol)
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    raw = "".join(lits)
    return raw.replace('\\"', '"').replace("\\\\", "\\").encode("ascii")


def descriptor_bytes_from(built):
    """Normalise build_descriptor()'s return value to bytes.

    The plan does not pin this contract, so accept the documented tuple (whose
    first element is the descriptor) or the descriptor itself as bytes/str.  Any
    other shape is reported as a readable failure: passing a dict through to the
    byte comparison would surface as a KeyError traceback instead.
    """
    if isinstance(built, tuple):
        if not built:
            raise SystemExit("build_descriptor() returned an empty tuple; "
                             "expected the descriptor bytes first")
        built = built[0]

    if isinstance(built, bytes):
        return built
    if isinstance(built, str):
        return built.encode("ascii")

    raise SystemExit("build_descriptor() returned %s; expected bytes, str, or a "
                     "tuple whose first element is one of those" % type(built).__name__)


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
        raise SystemExit("FAIL: official pyfibre package not found at %s" % pkg)
    sys.path.insert(0, str(pkg))
    try:
        import fibre.protocol as protocol
    except Exception as exc:                                  # pragma: no cover
        raise SystemExit("FAIL: could not import fibre.protocol from %s (%r)" % (pkg, exc))
    return protocol


# ---------------------------------------------------------------------------
# Faithful port of fibre_server.c: fibre_server_process_packet()
# ---------------------------------------------------------------------------

class FirmwareSim:
    """Port of the AT32 firmware's Fibre packet server."""

    def __init__(self, json_bytes, scalar_reads, property_widths=None, endpoint_kinds=None):
        self.json = json_bytes
        self.json_crc = firmware_crc16(1, json_bytes)   # fibre_server_init(): crc16_update(1u, ...)
        self.scalar_reads = scalar_reads                # endpoint_id -> bytes it would return
        self.writes = []                                # (endpoint_id, payload) offered for a property
        self.property_writes = []                       # as above, but only payloads of the declared width
        self.calls = []                                 # endpoint_ids dispatched as zero-argument calls
        self.sent = []                                  # responses actually emitted
        # Declared codec width per endpoint, taken from the SAME descriptor the
        # host builds its tree from.  Still a model of the Task 4 dispatcher, not
        # a verification of it (see property_writes below).
        self.property_widths = property_widths or {}
        # 'property' | 'function' | 'identity' per endpoint, also from the
        # descriptor.  The generated s_fibre_ep_kind[] draws the same
        # distinction, and without it a zero-length payload cannot be told apart
        # from an empty property write.  Unknown ids fall back to 'property',
        # which is what the pre-Task-5 server did for every payload.
        self.endpoint_kinds = endpoint_kinds or {}

    def process_packet(self, packet):
        """Returns the response bytes, or None (fibre_server.c:42-174)."""
        if packet is None or len(packet) < FIBRE_MIN_REQUEST_LENGTH:
            return None                                  # line 54-57

        seq_no = struct.unpack_from("<H", packet, 0)[0]
        endpoint_field = struct.unpack_from("<H", packet, 2)[0]
        output_length = struct.unpack_from("<H", packet, 4)[0]
        trailer = struct.unpack_from("<H", packet, len(packet) - 2)[0]

        endpoint_id = endpoint_field & 0x7FFF              # line 64
        # The last two bytes of every request are the trailer (line 62, 73), so
        # they are NOT payload: C bounds the payload with payload_length.
        payload_length = len(packet) - FIBRE_MIN_REQUEST_LENGTH
        payload = packet[6:6 + payload_length]

        expected_trailer = FIBRE_PROTOCOL_VERSION if endpoint_id == 0 else self.json_crc
        if trailer != expected_trailer:
            return None                                    # line 73-77

        response = bytearray(2)
        response_length = 2                                # line 52

        if endpoint_id == 0:
            if payload_length < 4:
                return None                                # line 85-88
            offset = struct.unpack_from("<I", payload, 0)[0]
            if offset == 0xFFFFFFFF:
                # Spec-defined sentinel (fibre_server.c:92-121): the descriptor's
                # 32-bit version id instead of a slice of the descriptor, and only
                # when the client asked for at least those 4 bytes.
                if output_length >= 4:
                    version_id = ((self.json_crc & 0xFFFF) << 16) | \
                                 firmware_crc16(self.json_crc & 0xFFFF, self.json)
                    response[2:6] = struct.pack("<I", version_id & 0xFFFFFFFF)
                    response_length = 6
                return self._maybe_send(endpoint_field, seq_no, response, response_length)
            if offset < len(self.json) and output_length > 0:
                available = len(self.json) - offset
                copy_length = min(available, output_length, FIBRE_MAX_RESPONSE_DATA)
                response[2:2 + copy_length] = self.json[offset:offset + copy_length]
                response_length = 2 + copy_length
                return self._maybe_send(endpoint_field, seq_no, response, response_length)
            return self._maybe_send(endpoint_field, seq_no, response, response_length)
        else:
            # fibre_server.c:142-152 -- the payload is offered UNCONDITIONALLY,
            # empty payloads included.  Gating this on `payload_length > 0` (the
            # pre-Task-5 code) is the defect that made a zero-argument function
            # endpoint a silent no-op; the endpoint layer, not the server, decides
            # whether a length is a write or a call.
            kind = self.endpoint_kinds.get(endpoint_id, "property")

            if kind == "function":
                # Zero-argument function: called with an empty payload, returns no
                # value.  Kept in its own list -- `writes` has always meant the
                # property payloads the server handed over, and a call must not be
                # mistaken for one.
                if payload_length == 0:
                    self.calls.append(endpoint_id)
            elif kind == "identity":
                pass                                       # read-only: never written
            else:
                # A property is written only when the payload carries exactly its
                # declared codec width; anything else is neither a write nor a
                # call.  `writes` records what the server offered (unchanged since
                # before Task 5), `property_writes` only what passed the width
                # rule -- see the width NOTE printed below.
                if payload_length > 0:
                    self.writes.append((endpoint_id, bytes(payload)))
                if payload_length == self.property_widths.get(endpoint_id):
                    self.property_writes.append((endpoint_id, bytes(payload)))

            if output_length > 0:
                max_length = min(output_length, FIBRE_MAX_RESPONSE_DATA)
                data = self.scalar_reads.get(endpoint_id, b"")   # default: 0 consumed
                data = data[:max_length]
                response[2:2 + len(data)] = data
                response_length = 2 + len(data)
            return self._maybe_send(endpoint_field, seq_no, response, response_length)

    def _maybe_send(self, endpoint_field, seq_no, response, response_length):
        if endpoint_field & 0x8000:                        # line 169-173
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

def walk_tree(top):
    """Flatten a descriptor tree into (objects, properties, functions, by_name).

    A leaf is either a property (a codec `type` and an `id`) or a function
    (`type` == "function", no codec).  Both are addressable by the host, but only
    properties have a codec to validate.  `by_name` maps a full dotted path to
    the member dict, which is how endpoint ids are resolved without ever
    hard-coding a number.
    """
    objects, props, funcs = [], [], []
    by_name = {}

    def walk(nodes, prefix):
        for entry in nodes:
            name = entry.get("name")
            path = name if not prefix else prefix + "." + name
            if isinstance(name, str):
                by_name[path] = entry
            if entry.get("type") == "object":
                objects.append(path)
                walk(entry.get("members", []), path)
            elif entry.get("type") == "function":
                funcs.append(path)
            else:
                props.append(path)

    if isinstance(top, list):
        walk(top, "")
    return objects, props, funcs, by_name


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


def first_property(by_name, want_writable=None, want_readable=None, want_codec=None):
    """Resolve a property by its declared capabilities, never by a literal id.

    Generated ids follow s_params[] table order, so any hard-coded number here
    would silently point at a different property after a param.c edit.
    """
    for path, entry in by_name.items():
        if entry.get("type") in (None, "object", "function"):
            continue
        if want_codec is not None and entry.get("type") != want_codec:
            continue
        access = entry.get("access") or ""
        if want_writable is not None and (access == "rw") != want_writable:
            continue
        if want_readable is not None and ("r" in access) != want_readable:
            continue
        return path, entry
    return None, None


def codec_width_of(entry):
    return CODEC_WIDTH.get(entry.get("type"))


def find_function(by_name, name):
    """Resolve a function endpoint by NAME, never by a literal id.

    Generated ids follow s_params[] table order, so a hard-coded number would
    silently address a different endpoint after any param.c edit.  Returns the
    (path, entry) pair, or (None, None) when the tree has no such function.
    """
    exact = by_name.get(name)
    if exact is not None and exact.get("type") == "function" and "id" in exact:
        return name, exact

    matches = [(path, entry) for path, entry in by_name.items()
               if entry.get("type") == "function" and "id" in entry
               and path.split(".")[-1] == name]
    if len(matches) == 1:
        return matches[0]
    return None, None


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
    print("[inputs] generator + generated header")
    # Report every missing input, not just the first one: while the generator is
    # absent the header is absent too, and one aggregated failure list is what
    # makes this readable at the TDD stage.
    try:
        gen = load_generator()
    except SystemExit as exc:
        gen = fail(str(exc))
    report_missing_inputs()
    if gen is None or not all(p.is_file() for p in (PARAM_C, GENERATED_H)):
        print()
        print("      Expected at this stage: the generator and the generated header "
              "are created by later tasks.")
        return finish()

    # ------------------------------------------------------------------
    print()
    print("[1] Descriptor bytes: independent re-parse vs the committed header")

    param_text = PARAM_C.read_text(encoding="utf-8")
    rows = gen.parse_param_rows(param_text)
    expected = descriptor_bytes_from(gen.build_descriptor(rows))

    header_text = GENERATED_H.read_text(encoding="utf-8")
    committed = c_string_literals_to_bytes(header_text, "s_fibre_json")

    print("        s_params[] rows      : %d" % len(rows))
    print("        expected descriptor  : %d bytes" % len(expected))
    print("        committed descriptor : %d bytes" % len(committed))

    same = committed == expected
    check("s_fibre_json matches the re-parse of param.c byte for byte", same,
          "" if same else describe_first_difference(expected, committed))

    # The device serves `sizeof(s_fibre_json) - 1` bytes, and the host sizes its
    # read loop from FIBRE_JSON_LENGTH, so a disagreement silently truncates the
    # descriptor (the host would then hash a prefix and reject every request).
    length_macro = re.search(r"#define\s+FIBRE_JSON_LENGTH\s+(.+)", header_text)
    if length_macro is None:
        check("FIBRE_JSON_LENGTH == len(descriptor)", False,
              "no #define FIBRE_JSON_LENGTH in the generated header")
    else:
        expression = length_macro.group(1).split("/*")[0].strip()
        literal = re.fullmatch(r"\(?\(?(?:uint32_t\)?)?\(?\s*(\d+)\s*u?\s*\)?\)?", expression)
        if literal is None:
            check("FIBRE_JSON_LENGTH == len(descriptor)", False,
                  "expected a numeric literal, got %r -- sizeof() cannot be checked "
                  "against the reconstructed byte count" % expression)
        else:
            declared = int(literal.group(1))
            check("FIBRE_JSON_LENGTH == len(descriptor)", declared == len(committed),
                  "macro says %d, descriptor is %d bytes" % (declared, len(committed)))

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
          all(b < 0x80 for b in committed) and b"\x00" not in committed)

    check("top level is a list", isinstance(top, list),
          "got %s" % type(top).__name__)
    if not isinstance(top, list):
        return finish()

    objects, paths, functions, by_name = walk_tree(top)
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

    # Root-level only: a nested axis0.config.serial_number must NOT satisfy this,
    # because the host looks these up at the root of the tree.
    root_leaf_names = [e.get("name") for e in top if e.get("type") != "object"]
    missing_identity = [name for name in IDENTITY_ENDPOINTS if name not in root_leaf_names]
    check("root-level identity endpoints present", not missing_identity,
          ("missing at root: %s" % missing_identity) if missing_identity
          else ", ".join(IDENTITY_ENDPOINTS))

    check("fw_version_major / fw_version_minor present at root",
          "fw_version_major" in root_leaf_names and "fw_version_minor" in root_leaf_names)

    # ------------------------------------------------------------------
    print()
    print("[3] CRC-16: official module vs protocol.rst vectors vs the firmware algorithm")

    protocol = load_official_protocol()

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

    json_crc = firmware_crc16(PROTOCOL_VERSION, committed)
    official_crc = protocol.calc_crc16(PROTOCOL_VERSION, committed)
    print("        json_crc(poly 0x3D65, init 1) = 0x%04X" % json_crc)
    check("firmware port == official calc_crc16 for the descriptor", json_crc == official_crc,
          "firmware=0x%04X official=0x%04X" % (json_crc, official_crc))

    m = re.search(r"json\s+crc\s*:\s*0x([0-9A-Fa-f]+)", header_text)
    check("generated header records the same json_crc",
          m is not None and int(m.group(1), 16) == json_crc,
          ("header 0x%04X vs computed 0x%04X" % (int(m.group(1), 16), json_crc)) if m
          else "no 'json crc : 0x....' line in the generated header")

    if not ok:
        print()
        print("      The CRC used for the descriptor trailer is wrong; every host "
              "request would be dropped.")

    # ------------------------------------------------------------------
    print()
    print("[6] Descriptor read handshake (official read loop vs firmware packet server)")

    property_widths = {entry["id"]: codec_width_of(entry) for entry in by_name.values()
                       if "id" in entry and entry.get("type") not in (None, "object", "function")}

    # Kind per endpoint, from the descriptor: a "function" member declares no
    # codec, the board identity block is the set of root members that have no
    # s_params[] row, everything else is a table-backed property.  This is what
    # makes the unconditional dispatch decidable: length 0 means "call" only for
    # a function endpoint, and only an exact codec-width payload is a write.
    identity_ids = {entry["id"] for entry in top
                    if entry.get("name") in IDENTITY_ENDPOINTS and "id" in entry}
    endpoint_kinds = {}
    for entry in iter_members(top):
        if "id" not in entry:
            continue
        if entry.get("type") == "function":
            endpoint_kinds[entry["id"]] = "function"
        elif entry["id"] in identity_ids:
            endpoint_kinds[entry["id"]] = "identity"
        else:
            endpoint_kinds[entry["id"]] = "property"

    device = FirmwareSim(committed, scalar_reads={}, property_widths=property_widths,
                         endpoint_kinds=endpoint_kinds)
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

    # The client asks for 512 bytes per chunk while the descriptor is far longer,
    # so a complete read proves the 61-byte server clamp is in effect.
    check("descriptor needed more round trips than the 61-byte clamp allows",
          len(committed) > FIBRE_MAX_RESPONSE_DATA and len(device.sent) > 1,
          "%d bytes over %d round trips" % (len(committed), len(device.sent)))

    # A host whose descriptor hashed differently must be ignored, not answered
    # with a tree it does not agree on.
    bogus = OfficialClient(FirmwareSim(committed, scalar_reads={}))
    bogus.interface_definition_crc = (device.json_crc ^ 0x0001) & 0xFFFF
    check("request with a mismatched json_crc is silently dropped",
          bogus.remote_endpoint_operation(1, b"", True, 4) is None)

    # Spec parity with the official endpoint0_handler(): offset 0xFFFFFFFF asks
    # for the descriptor's version id, not for a slice of the descriptor.  A host
    # that gets a slice (or an empty answer) here cannot tell the firmware's
    # object model apart from the one it read, which is exactly what this id
    # exists to decide.
    sentinel_device = FirmwareSim(committed, scalar_reads={})
    sentinel_client = OfficialClient(sentinel_device)
    sentinel_client.interface_definition_crc = sentinel_device.json_crc
    version_id = ((sentinel_device.json_crc & 0xFFFF) << 16) | \
                 firmware_crc16(sentinel_device.json_crc & 0xFFFF, committed)
    sentinel_answer = sentinel_client.remote_endpoint_operation(
        0, struct.pack("<I", 0xFFFFFFFF), True, 4)
    check("offset 0xFFFFFFFF returns the 4-byte json version id",
          sentinel_answer == struct.pack("<I", version_id),
          "got %r (%s bytes), expected %r (0x%08X)"
          % (sentinel_answer, len(sentinel_answer) if sentinel_answer is not None else None,
             struct.pack("<I", version_id), version_id))

    # ------------------------------------------------------------------
    print()
    print("[extra] Non-zero-endpoint traffic, endpoint resolved by NAME")

    read_path, read_entry = first_property(by_name, want_readable=True, want_codec="float")
    if read_path is None:
        check("a readable float property exists", False, "no readable float property in the tree")
    else:
        read_id = read_entry["id"]
        width = codec_width_of(read_entry)
        reading_device = FirmwareSim(committed, scalar_reads={read_id: b"\x00\x00\x80\x3F"},
                                     property_widths=property_widths)
        reader = OfficialClient(reading_device)
        reader.interface_definition_crc = reading_device.json_crc
        answer = reader.remote_endpoint_operation(read_id, b"", True, width)
        check("read of %s (id %d, %s) returns its declared width"
              % (read_path, read_id, read_entry["type"]),
              answer is not None and len(answer) == width,
              "got %s bytes, expected %d" % (len(answer) if answer is not None else None, width))

    write_path, write_entry = first_property(by_name, want_writable=True)
    if write_path is None:
        check("a writable property exists to write", False, "no rw property in the tree")
    else:
        write_id = write_entry["id"]
        width = codec_width_of(write_entry)
        payload = bytes([0xA5, 0x00, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A])[:width]

        writing_device = FirmwareSim(committed, scalar_reads={},
                                     property_widths=property_widths)
        writer = OfficialClient(writing_device)
        writer.interface_definition_crc = writing_device.json_crc

        writer.remote_endpoint_operation(write_id, payload, True, 0)
        check("write of %s (id %d, %s) delivers exactly its %d payload bytes"
              % (write_path, write_id, write_entry["type"], width),
              writing_device.property_writes == [(write_id, payload)],
              "recorded %r, expected %r" % (writing_device.property_writes, [(write_id, payload)]))

        # Regression guard for the exact off-by-two the responder caught: the
        # last 2 request bytes are the trailer and must not reach the endpoint.
        lengths = {endpoint_id: len(recorded) for endpoint_id, recorded in writing_device.writes}
        extra = {i: n - width for i, n in lengths.items() if n != width}
        check("the 2-byte request trailer is not part of the payload",
              not extra and lengths == {write_id: width},
              "recorded lengths %r (declared width %d, trailer is 2 bytes)"
              % (lengths, width))

        short_payload = payload[:max(1, width - 1)]
        short_device = FirmwareSim(committed, scalar_reads={},
                                   property_widths=property_widths)
        short_writer = OfficialClient(short_device)
        short_writer.interface_definition_crc = short_device.json_crc
        short_writer.remote_endpoint_operation(write_id, short_payload, True, 0)
        check("a payload that is not the declared width is not written",
              short_device.property_writes == [],
              "a %d-byte payload against a %d-byte codec: %r"
              % (len(short_payload), width, short_device.property_writes))

        # What this section does and does not prove, so nobody over-trusts it:
        # the wire traffic is model-independent (fibre_server.c:142-152), but the
        # width acceptance rule lives in fibre_endpoint_write() and is modelled
        # here from the descriptor's own declaration -- it is verified in Task 4.
        print("        NOTE: width acceptance is modelled from the descriptor "
              "(Task 4 verifies the dispatcher)")

    # The zero-argument function path.  This is the only evidence that the
    # pre-Task-5 `payload_length > 0` gate is gone: with that gate in place a
    # function call (always an EMPTY payload) never reached the endpoint layer at
    # all, so save_configuration() was a silent no-op, and every assertion below
    # fails if the gate comes back.  The endpoint id is resolved by NAME -- ids
    # follow s_params[] table order, so a literal would point elsewhere after a
    # param.c edit.
    function_path, function_entry = find_function(by_name, "save_configuration")
    if function_path is None:
        check("the save_configuration function endpoint exists in the tree", False,
              "no function endpoint named save_configuration")
    else:
        call_id = function_entry["id"]
        calling_device = FirmwareSim(committed, scalar_reads={},
                                     property_widths=property_widths,
                                     endpoint_kinds=endpoint_kinds)
        caller = OfficialClient(calling_device)
        caller.interface_definition_crc = calling_device.json_crc

        caller.remote_endpoint_operation(call_id, b"", True, 0)
        check("zero-argument %s (id %d) is dispatched, not silently skipped"
              % (function_path, call_id),
              calling_device.calls == [call_id],
              "dispatched calls %r, expected [%d]" % (calling_device.calls, call_id))
        check("calling %s does not write anything" % function_path,
              calling_device.writes == [] and calling_device.property_writes == [],
              "writes %r, property_writes %r"
              % (calling_device.writes, calling_device.property_writes))

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
