#!/usr/bin/env python3
"""Generate firmware/Inc/fibre_endpoints_generated.h from firmware/Src/param.c.

param.c is the single source of truth for the ODrive-style parameter namespace.
Hand-maintaining a second copy of those paths as a Fibre JSON descriptor would
drift, and a drifted descriptor fails *silently*: the host rejects every request
because the json_crc trailer no longer matches, and the symptom is "nothing
happens".

Usage:
  python tools/gen_fibre_endpoints.py           # write the header
  python tools/gen_fibre_endpoints.py --check   # exit 1 if out of date, write nothing

Public API, pinned by tools/fibre_conformance.py:
  parse_param_rows(text)  -> row dicts, in s_params[] table order
  build_descriptor(rows)  -> tuple whose FIRST element is the descriptor bytes

Everything here fails loudly (non-zero exit, message naming the path and the
line) rather than degrading silently: a half-understood table must never turn
into a descriptor the host will disagree with.
"""

import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARAM_C = ROOT / "firmware" / "Src" / "param.c"
OUT_H = ROOT / "firmware" / "Inc" / "fibre_endpoints_generated.h"

GEN_COMMAND = "python tools/gen_fibre_endpoints.py"

# param_kind_t -> Fibre codec string (design doc 4.2).  `float`, never `float32`.
KIND_TO_CODEC = {
    "PARAM_KIND_F32": "float",
    "PARAM_KIND_I32": "int32",
    "PARAM_KIND_U32": "uint32",
    "PARAM_KIND_U16": "uint16",
}

# Codec strings the official host understands; `float`, not `float32`
# (ODrive/tools/odrive/pyfibre ... legacy_object_client.cpp:182-194).
KNOWN_CODECS = frozenset([
    "bool", "int8", "uint8", "int16", "uint16",
    "int32", "uint32", "int64", "uint64", "float",
])

# Wire width of each codec, in bytes.  The host parses a response from the codec
# the descriptor declares, so a long or short answer shifts every later field.
CODEC_WIDTH = {"bool": 1, "int8": 1, "uint8": 1, "int16": 2, "uint16": 2,
               "int32": 4, "uint32": 4, "int64": 8, "uint64": 8, "float": 4}

# Sentinel stored in s_fibre_ep_index[] for endpoints whose value does not come
# from a s_params[] row.
FIBRE_EP_NOT_PARAM = 0xFFFF

EP_KIND_PROPERTY = 0
EP_KIND_IDENTITY = 1
EP_KIND_FUNCTION = 2
EP_KIND_NAMES = {
    EP_KIND_PROPERTY: "FIBRE_EP_KIND_PROPERTY",
    EP_KIND_IDENTITY: "FIBRE_EP_KIND_IDENTITY",
    EP_KIND_FUNCTION: "FIBRE_EP_KIND_FUNCTION",
}

# Endpoints the official host uses to identify the board.  They have NO param.c
# row, so they are listed here explicitly.  Their values come from the
# hand-written provider fibre_identity_read() in firmware/Src/fibre_endpoints.c
# -- they are NOT table-driven.  Do not "simplify" them away: without them the
# host cannot tell one board from another.
#
# A uint8 endpoint is ONE byte wide.  Writing two would make the host read the
# surplus byte as the next field, shifting the whole response.
IDENTITY_ENDPOINTS = [
    ("fw_version_revision", "uint8"),
    ("hw_version_major", "uint8"),
    ("hw_version_minor", "uint8"),
    ("hw_version_variant", "uint8"),
    ("serial_number", "uint64"),
]

# Zero-argument functions.  The official GUI's tuning dashboard calls
# save_configuration(); axis0.clear_errors is a table row whose read_ptr is NULL
# (so it is not a property) and whose on_write hook holds the real implementation.
FUNCTION_ENDPOINTS = ["save_configuration", "axis0.clear_errors"]

# path, alias, kind, precision, read_ptr, config_ptr, on_write, min, max, clamp
PARAM_FIELD_COUNT = 10

# path, id, type, access / inputs+outputs / members -- objects carry no id.
HEADER_GUARD = "AT32_ODRIVE_FIBRE_ENDPOINTS_GENERATED_H"


def fail(msg):
    sys.stderr.write("gen_fibre_endpoints: error: %s\n" % msg)
    sys.exit(1)


# ---------------------------------------------------------------------------
# C tokenizer: just enough of the language to read s_params[] without guessing
# ---------------------------------------------------------------------------

def skip_string(text, i):
    """`i` indexes an opening quote; return the index just past the closing one."""
    n = len(text)
    i += 1
    while i < n:
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == '"':
            return i + 1
        i += 1
    return n


def blank_out(text):
    """Same length as `text`, but newlines survive: line numbers must not move."""
    return "".join("\n" if ch == "\n" else " " for ch in text)


def strip_comments(text):
    """Remove /*...*/ and //... but never inside a string literal.

    param.c has section-separator comments *between* table rows, so a naive
    brace scan would be derailed by any '{' or ',' inside them.  Every comment is
    replaced by spaces of the same length, newlines included, so an offset in the
    result still maps onto the same line of the original -- the error messages
    quote line numbers, and a shifted line number is worse than none.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            end = skip_string(text, i)
            if end > n or text[end - 1] != '"':
                fail("unterminated string literal at line %d" % line_of(text, i))
            out.append(text[i:end])
            i = end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                fail("unterminated block comment at line %d" % line_of(text, i))
            out.append(blank_out(text[i:end + 2]))
            i = end + 2
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(blank_out(text[i:end]))
            i = end
            continue
        out.append(c)
        i += 1
    return "".join(out)


def extract_initializer_body(text, name):
    """Body between the outermost braces of `<name> = { ... };` and its offset."""
    idx = text.find(name)
    if idx < 0:
        fail("could not find %s in the source" % name)
    start = text.find("{", idx)
    if start < 0:
        fail("could not find '{' after %s" % name)
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], start + 1
    fail("unbalanced braces after %s" % name)


def split_top_level_spans(text):
    """Split on commas at brace/paren depth 0, respecting string literals.

    Returns [(offset_of_item, item)] with empty items dropped, so a caller can
    report the line a malformed row came from.
    """
    spans, start, depth, i, n = [], 0, 0, 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i = skip_string(text, i)
            continue
        if c in "{[(":
            depth += 1
        elif c in "}])":
            depth -= 1
        elif c == "," and depth == 0:
            spans.append((start, i))
            start = i + 1
        i += 1
    spans.append((start, n))

    items = []
    for a, b in spans:
        segment = text[a:b]
        item = segment.strip()
        if item:
            items.append((a + (len(segment) - len(segment.lstrip())), item))
    return items


def split_top_level(text):
    return [item for _, item in split_top_level_spans(text)]


def line_of(text, offset):
    return text.count("\n", 0, max(0, offset)) + 1

def is_identifier(name):
    if not name:
        return False
    if not (name[0].isascii() and (name[0].isalpha() or name[0] == "_")):
        return False
    return all(ch.isascii() and (ch.isalnum() or ch == "_") for ch in name)


def validate_path(path, where):
    """Every segment must be a plain C identifier, so enum/JSON escaping is moot."""
    segments = path.split(".")
    if not all(is_identifier(seg) for seg in segments):
        fail("%s: %r is not a dotted identifier path" % (where, path))


# ---------------------------------------------------------------------------
# param.c -> rows
# ---------------------------------------------------------------------------

def parse_param_rows(text):
    """Rows of s_params[], in table order, so index == index into s_params[].

    Brace-group based, never line based: rows span several lines, and the fields
    inside a row are separated by top-level commas that only the tokenizer above
    can tell apart from the commas between rows.
    """
    stripped = strip_comments(text)
    body, body_offset = extract_initializer_body(stripped, "s_params[]")
    spans = split_top_level_spans(body)
    if not spans:
        fail("s_params[] has an empty initializer")

    rows = []
    for span_offset, raw in spans:
        line = line_of(stripped, body_offset + span_offset)
        where = "line %d" % line
        if not (raw.startswith("{") and raw.endswith("}")):
            fail("%s: row is not a braced initializer (row %d of s_params[]): %s"
                 % (where, len(rows) + 1, raw[:120]))

        fields = split_top_level(raw[1:-1])
        if len(fields) != PARAM_FIELD_COUNT:
            fail("%s: row has %d fields, expected %d (path, alias, kind, precision, "
                 "read_ptr, config_ptr, on_write, min, max, clamp): %s"
                 % (where, len(fields), PARAM_FIELD_COUNT, raw[:120]))

        path_literal = fields[0]
        if (len(path_literal) < 2 or path_literal[0] != '"'
                or path_literal[-1] != '"' or '"' in path_literal[1:-1]
                or "\\" in path_literal):
            fail("%s: canonical path is not a plain string literal: %s"
                 % (where, path_literal[:60]))
        path = path_literal[1:-1]
        validate_path(path, where + " (canonical path)")

        kind = fields[2]
        if kind not in KIND_TO_CODEC:
            fail("%s: unknown kind %r for path %r -- add it to KIND_TO_CODEC "
                 "(and to param_kind_t handling in param.c)" % (where, kind, path))

        read_ptr = fields[4]
        config_ptr = fields[5]
        on_write = fields[6]
        # Cheap shape check that catches a mis-parse (a shifted field order would
        # otherwise silently change which endpoints are writable).
        if read_ptr != "NULL" and not read_ptr.startswith("&"):
            fail("%s: read_ptr for %r is neither NULL nor '&...': %s"
                 % (where, path, read_ptr[:60]))
        if config_ptr != "NULL" and not config_ptr.startswith("&"):
            fail("%s: config_ptr for %r is neither NULL nor '&...': %s"
                 % (where, path, config_ptr[:60]))
        if on_write != "NULL" and not is_identifier(on_write):
            fail("%s: on_write for %r is neither NULL nor an identifier: %s"
                 % (where, path, on_write[:60]))

        rows.append({
            "index": len(rows),
            "path": path,
            "kind": kind,
            "readable": read_ptr != "NULL",
            # The predicate param.c itself uses to return PARAM_RESULT_READ_ONLY.
            "writable": (config_ptr != "NULL") or (on_write != "NULL"),
            "line": line,
        })

    if len(rows) != len(spans):
        fail("parsed %d rows but s_params[] holds %d top-level brace groups"
             % (len(rows), len(spans)))
    if not rows:
        fail("no rows parsed from s_params[]")

    seen = {}
    for row in rows:
        if row["path"] in seen:
            fail("duplicate path %r (lines %d and %d)"
                 % (row["path"], seen[row["path"]]["line"], row["line"]))
        seen[row["path"]] = row

    for row in rows:
        if not row["readable"] and row["path"] not in FUNCTION_ENDPOINTS:
            fail("line %d: row %r has read_ptr == NULL, so it is not a property, and it "
                 "is not in FUNCTION_ENDPOINTS -- declare it there (or give the row a "
                 "read_ptr)" % (row["line"], row["path"]))

    for path in FUNCTION_ENDPOINTS:
        row = seen.get(path)
        if row is not None and row["readable"]:
            fail("%r is declared as a zero-argument function in FUNCTION_ENDPOINTS but "
                 "its s_params[] row (line %d) has a read_ptr; a function endpoint "
                 "takes no payload and returns no value" % (path, row["line"]))

    return rows


# ---------------------------------------------------------------------------
# rows -> descriptor
# ---------------------------------------------------------------------------

class Node:
    """One node of the endpoint trie: an object, or a leaf (property/function)."""

    __slots__ = ("name", "children", "leaf")

    def __init__(self, name):
        self.name = name
        self.children = {}
        self.leaf = None        # the JSON member dict, when this node is a leaf


def endpoint_member(name, role, codec, access, endpoint_id):
    """One leaf member, keys in the order the host reads them."""
    if role == EP_KIND_FUNCTION:
        return {"name": name, "id": endpoint_id, "type": "function",
                "inputs": [], "outputs": []}
    return {"name": name, "id": endpoint_id, "type": codec, "access": access}


def build_descriptor(rows):
    """Turn s_params[] rows into the compact Fibre 0.1 descriptor.

    Returns (descriptor_bytes, members, id_of_path, index_table, enum_names,
    kind_table, width_table).  Element 0 is the descriptor; that is the only
    element tools/fibre_conformance.py pins.

    id order: param.c rows in table order -> the identity table -> functions.
    """
    row_by_path = {row["path"]: row for row in rows}

    plan = []                           # (path, name, role, codec, access, row_index)
    for row in rows:
        if not row["readable"]:
            continue                    # function rows are added from the table below
        plan.append((row["path"], row["path"].split(".")[-1], EP_KIND_PROPERTY,
                     KIND_TO_CODEC[row["kind"]], "rw" if row["writable"] else "r",
                     row["index"]))

    for path, codec in IDENTITY_ENDPOINTS:
        plan.append((path, path, EP_KIND_IDENTITY, codec, "r", FIBRE_EP_NOT_PARAM))

    for path in FUNCTION_ENDPOINTS:
        row = row_by_path.get(path)
        # axis0.clear_errors has a real row: its on_write hook is the only
        # implementation of clear-errors, so the dispatcher needs that index.
        # save_configuration has no row at all.
        plan.append((path, path.split(".")[-1], EP_KIND_FUNCTION, None, None,
                     row["index"] if row is not None else FIBRE_EP_NOT_PARAM))

    seen = {}
    for path, _, _, _, _, _ in plan:
        if path in seen:
            fail("endpoint path %r is declared twice (param.c row and/or the identity "
                 "or function table)" % path)
        seen[path] = True
        validate_path(path, "generated endpoint")

    # --- id assignment, then the trie ------------------------------------
    root = Node("")
    index_table = [0]
    kind_table = [EP_KIND_PROPERTY]
    width_table = [0]
    enum_names = []
    id_of_path = {}

    for endpoint_id, (path, name, role, codec, access, row_index) in enumerate(plan, 1):
        member = endpoint_member(name, role, codec, access, endpoint_id)

        node = root
        segments = path.split(".")
        for depth, segment in enumerate(segments):
            last = depth == len(segments) - 1
            traversed = ".".join(segments[:depth])
            child = node.children.get(segment)
            if child is None:
                child = Node(segment)
                node.children[segment] = child
            if last:
                if child.leaf is not None:
                    fail("endpoint path %r is declared twice" % path)
                if child.children:
                    fail("%r is a leaf endpoint, but %r.%s.* also exists, so it would "
                         "have to be an object as well"
                         % (path, traversed, next(iter(child.children))))
                child.leaf = member
            elif child.leaf is not None:
                fail("%r is a leaf endpoint, but %r needs it to be an object"
                     % (".".join(segments[:depth + 1]), path))
            node = child

        enum_name = "FIBRE_EP_" + path.upper().replace(".", "_")
        if enum_name in enum_names or enum_name in ("FIBRE_EP_COUNT", "FIBRE_EP_NOT_PARAM"):
            fail("endpoint %r maps onto the reserved or already used enum name %s"
                 % (path, enum_name))
        enum_names.append(enum_name)
        id_of_path[path] = endpoint_id

        index_table.append(row_index)
        kind_table.append(role)
        width_table.append(0 if role == EP_KIND_FUNCTION else CODEC_WIDTH[codec])

    if not plan:
        fail("no endpoints to describe")

    unknown = sorted({e[3] for e in plan if e[3] is not None} - KNOWN_CODECS)
    if unknown:
        fail("codec(s) outside the official host whitelist: %s" % unknown)

    members = [member_json(child) for child in root.children.values()]
    descriptor = "[" + ",".join(json_compact(m) for m in members) + "]"

    try:
        data = descriptor.encode("ascii")
    except UnicodeEncodeError as exc:
        fail("descriptor is not ASCII: %s" % exc)
    if b"\x00" in data:
        fail("descriptor contains a NUL byte")

    # Self-check before anything is written: the bytes we are about to commit
    # must parse back into exactly the ids we assigned.
    try:
        parsed = json.loads(data)
    except ValueError as exc:
        fail("generated descriptor is not valid JSON (%s): %s" % (exc, descriptor[:200]))
    found_ids = sorted(entry["id"] for entry in iter_members(parsed) if "id" in entry)
    if found_ids != list(range(1, len(plan) + 1)):
        fail("generated descriptor declares ids %s, expected 1..%d"
             % (found_ids[:8], len(plan)))

    return (data, members, id_of_path, index_table, enum_names, kind_table, width_table)


def json_compact(obj):
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=True)


def member_json(node):
    if node.leaf is not None:
        return node.leaf
    return {"name": node.name, "type": "object",
            "members": [member_json(child) for child in node.children.values()]}


def iter_members(nodes):
    for entry in nodes:
        yield entry
        if entry.get("type") == "object":
            for child in iter_members(entry.get("members", [])):
                yield child


# ---------------------------------------------------------------------------
# Descriptor CRC (port of firmware/Src/crc.c: poly 0x3D65, MSB-first)
# ---------------------------------------------------------------------------

def firmware_crc16(crc, data):
    for byte in data:
        crc ^= (byte << 8) & 0xFFFF
        for _ in range(8):
            crc = ((crc << 1) ^ 0x3D65) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

def c_literal(text):
    out = []
    for ch in text:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif 0x20 <= ord(ch) < 0x7F:
            out.append(ch)
        else:
            fail("non-ASCII character %r in the descriptor" % ch)
    return '"' + "".join(out) + '"'


def chunked(values, per_line, formatter):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("  " + " ".join(formatter(v) for v in values[i:i + per_line]))
    return lines


def render_header(param_bytes, rows, descriptor, members, index_table, enum_names,
                  kind_table, width_table):
    source_hash = hashlib.sha256(param_bytes).hexdigest()
    json_crc = firmware_crc16(1, descriptor)
    count = len(enum_names)

    out = []
    out.append("/*")
    out.append(" * AUTOGENERATED by tools/gen_fibre_endpoints.py -- DO NOT EDIT BY HAND.")
    out.append(" *")
    out.append(" * command     : %s" % GEN_COMMAND)
    out.append(" * source      : firmware/Src/param.c  (sha256 %s, %d rows)"
               % (source_hash, len(rows)))
    out.append(" * json bytes  : %d" % len(descriptor))
    out.append(" * json crc    : 0x%04X   (poly 0x3D65, init 1, MSB-first -- see firmware/Src/crc.c)"
               % json_crc)
    out.append(" * endpoint ids: %d" % count)
    out.append(" *")
    out.append(" * Endpoints whose value does NOT come from the param table (fw_version_revision,")
    out.append(" * hw_version_*, serial_number) are marked FIBRE_EP_NOT_PARAM and are served by the")
    out.append(" * hand-written provider fibre_identity_read() in firmware/Src/fibre_endpoints.c.")
    out.append(" * Regenerate after ANY change to the s_params[] table: a stale descriptor is not")
    out.append(" * rejected with an error, every host request is simply dropped.")
    out.append(" *")
    out.append(" * s_fibre_ep_index[id] is the row this endpoint reads/writes in s_params[], or")
    out.append(" * FIBRE_EP_NOT_PARAM when there is no such row.  axis0.clear_errors keeps its row")
    out.append(" * index because its on_write hook is the only implementation of clear-errors.")
    out.append(" * s_fibre_ep_kind[id] tells the dispatcher how to treat the payload: a property is")
    out.append(" * written only when the payload matches s_fibre_ep_width[id], while a zero-argument")
    out.append(" * function is called with an empty payload (width 0 -- it declares no codec).")
    out.append(" */")
    out.append("#ifndef %s" % HEADER_GUARD)
    out.append("#define %s" % HEADER_GUARD)
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("#define FIBRE_EP_NOT_PARAM   ((uint16_t)0xFFFFu)")
    out.append("")
    out.append("/* What the dispatcher must do with this endpoint.  Without this the dispatcher")
    out.append(" * cannot tell a property from a zero-argument function: both have an entry in")
    out.append(" * s_fibre_ep_index[], but a function must be called with an EMPTY payload while")
    out.append(" * a property is written only when the payload matches its codec width. */")
    out.append("typedef enum")
    out.append("{")
    out.append("  FIBRE_EP_KIND_PROPERTY = 0,   /* s_fibre_ep_index[] = row in s_params[]     */")
    out.append("  FIBRE_EP_KIND_IDENTITY = 1,   /* value from fibre_identity_read()           */")
    out.append("  FIBRE_EP_KIND_FUNCTION = 2,   /* call it; s_fibre_ep_index[] = row whose")
    out.append("                                   on_write implements it, or NOT_PARAM       */")
    out.append("} fibre_ep_kind_t;")
    out.append("")
    out.append("/* One entry per endpoint, in id order; the value IS the endpoint id. */")
    out.append("typedef enum")
    out.append("{")
    for i, name in enumerate(enum_names):
        out.append("  %s = %d," % (name, i + 1))
    out.append("} fibre_ep_id_t;")
    out.append("")
    out.append("#define FIBRE_EP_COUNT   (%du)" % count)
    out.append("")
    out.append("/*")
    out.append(" * Compact JSON descriptor.  This header is included by exactly one translation")
    out.append(" * unit (firmware/Src/fibre_endpoints.c); keeping the array `static` here avoids")
    out.append(" * a second copy in flash.")
    out.append(" */")
    out.append("static const char s_fibre_json[] =")
    out.append('  "["')
    for i, member in enumerate(members):
        # One C literal per top-level member: the concatenation of all of them is
        # byte for byte the compact JSON the CRC is computed over.
        text = json_compact(member) + ("," if i + 1 < len(members) else "")
        out.append("  " + c_literal(text))
    out.append('  "]";')
    out.append("")
    out.append("/* Generated NUMERIC LITERAL, not `sizeof(s_fibre_json) - 1`: the same generator")
    out.append(" * run that emits the bytes emits this count, so it cannot go stale. */")
    out.append("#define FIBRE_JSON_LENGTH   (%du)" % len(descriptor))
    out.append("")
    out.append("/* id -> index into s_params[], or FIBRE_EP_NOT_PARAM.  Index 0 is unused. */")
    out.append("static const uint16_t s_fibre_ep_index[FIBRE_EP_COUNT + 1u] =")
    out.append("{")
    out.append("  0u,")
    out.extend(chunked(index_table[1:], 4, lambda v: "FIBRE_EP_NOT_PARAM," if v == FIBRE_EP_NOT_PARAM else "%du," % v))
    out.append("};")
    out.append("")
    out.append("/* id -> fibre_ep_kind_t.  Index 0 is unused. */")
    out.append("static const uint8_t s_fibre_ep_kind[FIBRE_EP_COUNT + 1u] =")
    out.append("{")
    out.append("  %s," % EP_KIND_NAMES[EP_KIND_PROPERTY])
    out.extend(chunked(kind_table[1:], 4, lambda v: EP_KIND_NAMES[v] + ","))
    out.append("};")
    out.append("")
    out.append("/* id -> payload byte width of the declared codec (1/2/4/8; 0 for a function,")
    out.append(" * which declares no codec).  Used to size the response and to validate a write,")
    out.append(" * so no code has to hard-code a width. */")
    out.append("static const uint8_t s_fibre_ep_width[FIBRE_EP_COUNT + 1u] =")
    out.append("{")
    out.append("  0u,")
    out.extend(chunked(width_table[1:], 8, lambda v: "%du," % v))
    out.append("};")
    out.append("")
    out.append("#endif /* %s */" % HEADER_GUARD)
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

USAGE = "usage: gen_fibre_endpoints.py [--check]"


def main(argv):
    check_only = False
    for arg in argv:
        if arg == "--check":
            check_only = True
        elif arg in ("-h", "--help"):
            print(__doc__.strip())
            return 0
        else:
            fail("unknown argument %r (%s)" % (arg, USAGE))

    if not PARAM_C.is_file():
        fail("input %s does not exist" % PARAM_C)
    param_bytes = PARAM_C.read_bytes()
    try:
        param_text = param_bytes.decode("utf-8")
    except UnicodeDecodeError as exc:
        fail("%s is not valid UTF-8: %s" % (PARAM_C.relative_to(ROOT), exc))

    rows = parse_param_rows(param_text)
    (descriptor, members, _id_of_path, index_table, enum_names,
     kind_table, width_table) = build_descriptor(rows)
    header = render_header(param_bytes, rows, descriptor, members, index_table,
                           enum_names, kind_table, width_table)
    wanted = header.encode("utf-8")

    rel_out = OUT_H.relative_to(ROOT).as_posix()
    if check_only:
        if not OUT_H.is_file():
            fail("%s does not exist; run: %s" % (rel_out, GEN_COMMAND))
        on_disk = OUT_H.read_bytes()
        if on_disk != wanted:
            fail("%s is out of date (param.c has changed since it was generated, or it was "
                 "edited by hand); run: %s" % (rel_out, GEN_COMMAND))
        return 0

    OUT_H.write_bytes(wanted)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
