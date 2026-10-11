# Fibre 端点树补全 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让官方开源 ODrive GUI 能完整使用本板固件——由 `firmware/Src/param.c` 的 `s_params[]` 生成 Fibre 0.1 端点树，补上函数端点与规范缺口，并跑通 Keil 全量编译。

**Architecture:** `param.c` 的表是唯一事实源。一个 Python 生成器把它转成 `firmware/Inc/fibre_endpoints_generated.h`（紧凑 JSON 描述符 + 端点 id 枚举 + id→表行索引），`fibre_endpoints.c` 瘦成薄分发层，写路径全部经 `param_write_entry()` 复用既有的范围检查与 `nvm_config_apply()`。**只生成头文件**，因此不动 `tools/gen_keil_project.py` 的 `USER_SOURCES`。

**Tech Stack:** C（AT32F435 / Keil ARMCC，AGENTS.md 要求 0 warning）、Python 3.12（生成器与一致性测试，不需要主机 C 工具链）、Fibre 0.1 协议。

**设计文档:** `docs/superpowers/specs/2026-10-11-fibre-full-endpoint-tree-design.md`（已批准）

---

## 执行者必须先知道的环境事实

| 事实 | 影响 |
| --- | --- |
| 本机**没有 `make`/`mingw32-make`**，因此 `firmware/Makefile` 与 `tests/Makefile` 都跑不了 | 不要执行 `mingw32-make`；用下面的替代验证 |
| 本机**有** `arm-none-eabi-gcc` 10.3.1 | 用它做 `-fsyntax-only` 语法检查 |
| 本机**有** Keil：`D:\Keil\UV4\UV4.exe`（ARMCC + ARMCLANG） | 真机编译走它 |
| 本机**有** Python 3.12（`python` 在 PATH） | 生成器与一致性测试走它 |
| `node`/`npm` 必须显式用 `C:\Program Files\nodejs\` 下的版本 | 本任务不涉及；PATH 里 DSH 自带的 node v26 的 npm 是坏的 |
| 官方仓库克隆在 `D:\1-myproject\AT32_ODrive\ODrive`（仅测试参照，**不要修改它**） | 一致性测试要读它的 `fibre/protocol.py` 与 GUI 看板 JSON |

**语法检查命令模板**（每个 Task 的验证步骤都用这个形状，`<files>` 换成该 Task 改动的 .c 文件）：

```powershell
& "C:\Program Files (x86)\GNU Arm Embedded Toolchain\10 2021.10\bin\arm-none-eabi-gcc.exe" `
  -fsyntax-only -std=gnu11 -Wall -Wextra `
  -I firmware/Inc -I firmware/Drivers/CMSIS/Device/AT32F435_437 `
  -I firmware/Drivers/CMSIS/Core/Include -I firmware/Drivers/AT32F435_437_Firmware_Library/inc `
  -I firmware/Drivers/USB_Device/inc -I firmware/Drivers/USB_Device/inc `
  <files>
```

> **注意**：上述 `-I` 路径已核实存在。特别注意 `at32f435_437.h` 在
> `firmware\Drivers\CMSIS\Device\AT32F435_437`，**没有** `\inc` 子目录；写成 `...\inc` 会找不到头文件。

---

## 文件结构

| 动作 | 路径 | 职责 |
| --- | --- | --- |
| Create | `tools/gen_fibre_endpoints.py` | 解析 `param.c` → 生成头文件；`--check` 模式做漂移检查 |
| Create | `tools/fibre_conformance.py` | 无工具链一致性测试（生成器复算、id 唯一、codec 白名单、CRC、GUI 看板路径） |
| Create（生成、入库） | `firmware/Inc/fibre_endpoints_generated.h` | 描述符字符串 + 端点 id 枚举 + id→表行索引 |
| Modify | `firmware/Inc/param.h` | 声明 `param_value_t` / `param_read_entry` / `param_table_entries` / `param_write_entry` |
| Modify | `firmware/Src/param.c` | 实现上述四个 |
| Modify | `firmware/Src/fibre_endpoints.c` | 手写 switch → 表分发 + 身份端点 provider + 函数分发 |
| Modify | `firmware/Src/fibre_server.c` | 无条件分发写载荷；endpoint 0 的 `0xFFFFFFFF` 分支 |
| Modify | `firmware/Inc/fibre_server.h` | 无需改动（除非需要暴露 `json_version_id`） |
| Modify | `firmware/Makefile` | `gen-fibre` / `check-fibre` 目标 |
| Modify | `AGENTS.md`、`docs/odrive_mapping.md`、`docs/architecture.md` | 文档同步 |

**不新增 `firmware/Src/*.c`**，所以 `tools/gen_keil_project.py` 与 `AT32_ODrive.uvprojx` 都不需要改。

---

## Task 1: 一致性测试骨架（先失败）

**Files:**
- Create: `tools/fibre_conformance.py`
- Test: 它自己就是测试

- [ ] **Step 0: 核实 include 路径**

运行并记录输出，把下面的 `INCLUDES` 固定成实际存在的路径：

```powershell
Get-ChildItem firmware\Drivers -Recurse -Filter "at32f435_437.h" | Select-Object -ExpandProperty DirectoryName
Get-ChildItem firmware\Drivers -Recurse -Filter "core_cm4.h" | Select-Object -ExpandProperty DirectoryName
```

- [ ] **Step 1: 写测试（此时必然失败）**

创建 `tools/fibre_conformance.py`。它必须做到：

1. 独立复算：自己解析 `firmware/Src/param.c`，构建期望的紧凑 JSON 描述符；
2. 读回入库的 `firmware/Inc/fibre_endpoints_generated.h`，把 `s_fibre_json[]` 的 C 字面量还原成字节；
3. 断言两者**逐字节相等**；
4. 断言：合法 JSON、顶层是 list、id 唯一且 1..N 连续、每个 codec 都在官方白名单内、
   `hw_version_*`/`serial_number`/`fw_version_revision` 存在；
5. 用官方 `fibre.protocol.calc_crc16`（从 `ODrive/tools/odrive/pyfibre` 导入）与
   `ODrive/docs/protocol.rst` 的测试向量校验 CRC，并断言 CRC == 用固件算法（poly `0x3D65`，
   init `1`，MSB-first，无反射）算出的值；
6. 用固件包服务器的逐行端口驱动官方客户端的读描述符循环，断言完整取回、trailer 一致；
7. 断言 `ODrive/GUI/src/assets/dashboards/Tuning_0_5_1.json` 的全部路径都能在树里解析到。

**骨架（编号 1–4、6 的部分沿用上一轮已写好的临时脚本 `%TEMP%\fibre_conformance\check_fibre_compat.py`，
它已被实测跑通 12/12；把它复制过来并补齐 1、4、5、7）**：

```python
#!/usr/bin/env python3
"""Fibre descriptor conformance test - needs no C toolchain.

Cross-checks firmware/Inc/fibre_endpoints_generated.h against an independent
re-parse of firmware/Src/param.c, the official Python fibre implementation and
the official GUI's dashboard expectations.

Usage:  python tools/fibre_conformance.py
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ODRIVE = Path(r"D:\1-myproject\AT32_ODrive\ODrive")
GENERATED_H = ROOT / "firmware" / "Inc" / "fibre_endpoints_generated.h"

def load_generator():
    """The generator itself lands in Task 2.  Until then this test must fail with
    a readable message rather than an import traceback."""
    sys.path.insert(0, str(ROOT / "tools"))
    try:
        import gen_fibre_endpoints as gen
    except ImportError as exc:
        raise SystemExit("FAIL: tools/gen_fibre_endpoints.py not available yet (%s)" % exc)
    return gen

RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, ok, detail))
    print(("  PASS  " if ok else "  FAIL  ") + name + (("   -> " + detail) if detail else ""))


def c_string_literals_to_bytes(text, symbol):
    """Extract the concatenated C string literal(s) assigned to `symbol`."""
    m = re.search(re.escape(symbol) + r"\[\]\s*=\s*(.*?);", text, re.S)
    if not m:
        raise SystemExit("could not find %s in the generated header" % symbol)
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    raw = "".join(lits)
    return raw.replace('\\"', '"').replace("\\\\", "\\").encode("ascii")


def firmware_crc16(crc, data):
    """Port of firmware/Src/crc.c crc16_update_byte (poly 0x3D65, MSB-first)."""
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            crc = ((crc << 1) ^ 0x3D65) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc
```

（其余部分照搬临时脚本的 `FirmwareSim` / `OfficialClient` 两个类，并把 `extract_descriptor()`
换成上面的 `c_string_literals_to_bytes(text, "s_fibre_json")`。）

- [ ] **Step 2: 运行，确认失败**

Run: `python tools/fibre_conformance.py`
Expected: FAIL —— `FileNotFoundError` / `SystemExit: could not find s_fibre_json`，因为生成头文件还不存在。

- [ ] **Step 3: 提交**

```bash
git add tools/fibre_conformance.py
git commit -m "test: 新增 Fibre 描述符一致性测试（当前失败，等待生成器）"
```

---

## Task 2: 生成器与生成头文件

**Files:**
- Create: `tools/gen_fibre_endpoints.py`
- Create（生成）: `firmware/Inc/fibre_endpoints_generated.h`
- Test: `tools/fibre_conformance.py`

- [ ] **Step 1: 写生成器**

创建 `tools/gen_fibre_endpoints.py`。**关键实现要求**（都是踩过坑的点）：

```python
#!/usr/bin/env python3
"""Generate firmware/Inc/fibre_endpoints_generated.h from firmware/Src/param.c.

param.c is the single source of truth for the ODrive-style parameter namespace.
Hand-maintaining a second copy of those paths as a Fibre JSON descriptor would
drift, and a drifted descriptor fails *silently*: the host rejects every request
because the json_crc trailer no longer matches, and the symptom is "nothing
happens".

Usage:
  python tools/gen_fibre_endpoints.py           # write the header
  python tools/gen_fibre_endpoints.py --check   # exit 1 if out of date
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARAM_C = ROOT / "firmware" / "Src" / "param.c"
OUT_H = ROOT / "firmware" / "Inc" / "fibre_endpoints_generated.h"

KIND_TO_CODEC = {
    "PARAM_KIND_F32": "float",
    "PARAM_KIND_I32": "int32",
    "PARAM_KIND_U32": "uint32",
    "PARAM_KIND_U16": "uint16",
}

# Official codec strings recognised by the host (legacy_object_client.cpp:182-194).
KNOWN_CODECS = {"bool", "int8", "uint8", "int16", "uint16",
                "int32", "uint32", "int64", "uint64", "float"}

# Endpoints the official host uses to identify the board.  They have NO param.c
# row, so they are listed here explicitly.  Their values come from the
# hand-written provider fibre_identity_read() in firmware/Src/fibre_endpoints.c
# -- they are NOT table-driven.  Do not "simplify" them away.
IDENTITY_ENDPOINTS = [
    ("fw_version_revision", "uint8"),
    ("hw_version_major", "uint8"),
    ("hw_version_minor", "uint8"),
    ("hw_version_variant", "uint8"),
    ("serial_number", "uint64"),
]

# Zero-argument functions.  The GUI tuning dashboard calls save_configuration().
FUNCTION_ENDPOINTS = ["save_configuration", "axis0.clear_errors"]


def fail(msg):
    sys.stderr.write("gen_fibre_endpoints: error: %s\n" % msg)
    sys.exit(1)


def strip_comments(text):
    """Remove /*...*/ and //... but never inside a string literal.

    param.c has section-separator comments *between* table rows, so a naive
    brace scan would be derailed by any '{' or ',' inside them.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            out.append(c)
            i += 1
            while i < n:
                if text[i] == "\\":
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                fail("unterminated block comment")
            out.append(" " * (end + 2 - i))
            i = end + 2
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
            continue
        out.append(c)
        i += 1
    return "".join(out)


def extract_initializer_body(text, name):
    idx = text.find(name)
    if idx < 0:
        fail("could not find %s" % name)
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
                return text[start + 1:i]
    fail("unbalanced braces after %s" % name)


def split_top_level(text):
    """Split on commas at brace/paren depth 0, respecting string literals."""
    items, cur, depth, i, n = [], [], 0, 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            cur.append(c)
            i += 1
            while i < n:
                if text[i] == "\\":
                    cur.append(text[i:i + 2])
                    i += 2
                    continue
                cur.append(text[i])
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c in "{[(":
            depth += 1
        elif c in "}])":
            depth -= 1
        if c == "," and depth == 0:
            items.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    tail = "".join(cur).strip()
    if tail:
        items.append(tail)
    return items


def parse_param_rows(text):
    """Rows of s_params[], in table order, so index == index into s_params[]."""
    text = strip_comments(text)
    body = extract_initializer_body(text, "s_params")
    rows = []
    for raw in split_top_level(body):
        raw = raw.strip()
        if not raw:
            continue
        if not (raw.startswith("{") and raw.endswith("}")):
            fail("unexpected row shape: %s" % raw[:100])
        fields = split_top_level(raw[1:-1])
        if len(fields) < 7:
            fail("row has %d fields, expected >= 7: %s" % (len(fields), raw[:100]))
        path = fields[0]
        if not (path.startswith('"') and path.endswith('"')):
            fail("row path is not a string literal: %s" % path[:60])
        path = path[1:-1]
        kind = fields[2]
        if kind not in KIND_TO_CODEC:
            fail("unknown kind %r for path %r -- add it to KIND_TO_CODEC" % (kind, path))
        rows.append({
            "index": len(rows),
            "path": path,
            "kind": kind,
            "readable": fields[4] != "NULL",
            "writable": (fields[5] != "NULL") or (fields[6] != "NULL"),
        })
    if not rows:
        fail("no rows parsed from s_params[]")
    for r in rows:
        if not r["readable"] and r["path"] not in FUNCTION_ENDPOINTS:
            fail("row %r has read_ptr == NULL and is not a declared function endpoint"
                 % r["path"])
    return rows
```

`build_descriptor(rows)` 负责：

- `properties = [(path, codec, writable) for r in rows if r["readable"]] + IDENTITY_ENDPOINTS`；
- id 分配顺序：**① param.c 行（表顺序）→ ② 身份表 → ③ 函数表**，从 1 连续；
- 用点号路径构造嵌套 trie；遇到"某路径既是属性又是对象"或重复路径 → `fail()`；
- 生成紧凑 JSON（`json.dumps(..., separators=(",", ":"), ensure_ascii=True)`），
  顶层每个成员单独序列化后用 `,` 连接，这样生成 C 字面量时可以一行一个、且拼接结果逐字节等于紧凑 JSON；
- 返回 `(descriptor_bytes, members, id_of_path, index_table, enum_names)`。

`index_table[id]`：param.c 来源的属性 → `r["index"]`；身份端点与函数端点 → `0xFFFF`
（即 C 里的 `FIBRE_EP_NOT_PARAM`）。

- [ ] **Step 2: 生成头文件**

生成的 `firmware/Inc/fibre_endpoints_generated.h` 必须是这个形状：

```c
/*
 * AUTOGENERATED by tools/gen_fibre_endpoints.py -- DO NOT EDIT BY HAND.
 *
 * source      : firmware/Src/param.c  (sha256 <hash>, <N> rows)
 * json bytes  : <len>
 * json crc    : 0x<XXXX>   (poly 0x3D65, init 1, MSB-first -- see firmware/Src/crc.c)
 *
 * Endpoints whose value does NOT come from the param table (fw_version_revision,
 * hw_version_*, serial_number) are marked FIBRE_EP_NOT_PARAM and are served by the
 * hand-written provider fibre_identity_read() in firmware/Src/fibre_endpoints.c.
 * Regenerate after ANY change to the s_params[] table.
 */
#ifndef AT32_ODRIVE_FIBRE_ENDPOINTS_GENERATED_H
#define AT32_ODRIVE_FIBRE_ENDPOINTS_GENERATED_H

#include <stdint.h>

#define FIBRE_EP_NOT_PARAM   ((uint16_t)0xFFFFu)

/* What the dispatcher must do with this endpoint.  Without this the dispatcher
 * cannot tell a property from a zero-argument function: both have an entry in
 * s_fibre_ep_index[], but a function must be called with an EMPTY payload while
 * a property is written only when the payload matches its codec width. */
typedef enum
{
  FIBRE_EP_KIND_PROPERTY = 0,   /* s_fibre_ep_index[] = row in s_params[]     */
  FIBRE_EP_KIND_IDENTITY = 1,   /* value from fibre_identity_read()           */
  FIBRE_EP_KIND_FUNCTION = 2,   /* call it; s_fibre_ep_index[] = row whose
                                   on_write implements it, or NOT_PARAM       */
} fibre_ep_kind_t;

typedef enum
{
  FIBRE_EP_FW_VERSION_MAJOR = 1,
  /* ... one entry per endpoint, in id order ... */
} fibre_ep_id_t;

#define FIBRE_EP_COUNT   (<total>u)

/*
 * Compact JSON descriptor.  This header is included by exactly one translation
 * unit (firmware/Src/fibre_endpoints.c); keeping the array `static` here avoids
 * a second copy in flash.
 */
static const char s_fibre_json[] =
  "["
  "<escaped member>,"
  /* ... */
  "<escaped last member>"
  "]";

#define FIBRE_JSON_LENGTH   ((uint32_t)(sizeof(s_fibre_json) - 1u))

/* id -> index into s_params[], or FIBRE_EP_NOT_PARAM.  Index 0 is unused. */
static const uint16_t s_fibre_ep_index[FIBRE_EP_COUNT + 1u] =
{
  0u,
  /* ... */
};

/* id -> fibre_ep_kind_t.  Index 0 is unused. */
static const uint8_t s_fibre_ep_kind[FIBRE_EP_COUNT + 1u] =
{
  0u,
  /* ... */
};

/* id -> payload byte width of the declared codec (1/2/4/8).  Used to size the
 * response and to validate a write, so no code has to hard-code a width. */
static const uint8_t s_fibre_ep_width[FIBRE_EP_COUNT + 1u] =
{
  0u,
  /* ... */
};

#endif /* AT32_ODRIVE_FIBRE_ENDPOINTS_GENERATED_H */
```

**生成器必须遵守的一条规则**：`IDENTITY_ENDPOINTS` 里 `uint8` 的端点只写 **1 字节**。
`fw_version_revision` / `hw_version_*` 都是 `uint8` —— 写 2 字节会让上位机把多余字节当成下一个
字段，描述符 CRC 与响应长度都会错位。

Run: `python tools/gen_fibre_endpoints.py`
Expected: 无输出、退出码 0，生成头文件；头部注释里 `json bytes` 与 `json crc` 有具体数值。

- [ ] **Step 3: 跑一致性测试**

Run: `python tools/fibre_conformance.py`
Expected: 编号 1–5、7 的断言全 PASS（编号 6 的端到端握手也 PASS）。若 `json crc` 与测试算出的不一致，
**不要改测试**——那是生成器与 CRC 实现不一致，去查生成器。

- [ ] **Step 4: 用 `--check` 验证漂移检测真的会失败**

Run: `python tools/gen_fibre_endpoints.py --check` → 期望退出码 0。
然后手动在 `param.c` 里改一个 `max_value`（不改路径，不改结构），再跑 `--check` → 期望 0（因为只有路径/类型/读写性进描述符，边界值不进）。
再把某行 `&g_axis.pos_estimate` 临时改成 `NULL` → 期望 `--check` 退出码 1 且报错信息指出该行。
**改完必须还原。**

- [ ] **Step 5: 提交**

```bash
git add tools/gen_fibre_endpoints.py firmware/Inc/fibre_endpoints_generated.h tools/fibre_conformance.py
git commit -m "feat(fibre): 由 param.c 生成 Fibre 端点描述符"
```

---

## Task 3: `param.c` 的表访问与按行读写接口

**Files:**
- Modify: `firmware/Inc/param.h`（在 `param_write_value` 声明之后追加）
- Modify: `firmware/Src/param.c`（在 `param_write_value` 定义之后追加）
- Test: 语法检查

- [ ] **Step 1: 先在头文件定义接口**（AGENTS.md：新功能先在 `Inc` 定义接口，再在 `Src` 实现）

在 `firmware/Inc/param.h` 末尾 `#endif` 之前追加：

```c
/*
 * Structured read result.  The Fibre endpoint layer needs the *typed* value so
 * it can emit the wire width that the JSON descriptor promised (4 bytes for
 * float/int32/uint32, 2 bytes for uint16).  Keeping the kind->memory-layout
 * mapping here means no other module needs to know how a row is stored.
 */
typedef struct
{
  param_kind_t kind;
  float        f32;
  int32_t      i32;
  uint32_t     u32;
  uint16_t     u16;
} param_value_t;

/* Read one row by entry.  Returns PARAM_RESULT_READ_ONLY if the row has no
 * read_ptr, PARAM_RESULT_UNKNOWN for an unhandled kind. */
param_result_t param_read_entry(const param_entry_t *entry, param_value_t *out);

/* The canonical parameter table, in declaration order.  Lets the Fibre layer
 * index a row directly instead of doing a string search per polled variable. */
const param_entry_t *param_table_entries(uint32_t *count);

/* Write one row by entry.  A thin wrapper over the same store path as
 * param_write_value(), so bounds checks, nvm_config_apply() and the
 * pole_pairs/cpr calibration invalidation are shared, never duplicated. */
param_result_t param_write_entry(const param_entry_t *entry, float value);
```

- [ ] **Step 2: 实现**

在 `firmware/Src/param.c` 的 `param_write_value()` 定义之后追加：

```c
const param_entry_t *param_table_entries(uint32_t *count)
{
  if (count != NULL)
  {
    *count = (uint32_t)(sizeof(s_params) / sizeof(s_params[0]));
  }

  return s_params;
}

param_result_t param_read_entry(const param_entry_t *entry, param_value_t *out)
{
  if ((entry == NULL) || (out == NULL))
  {
    return PARAM_RESULT_UNKNOWN;
  }

  if (entry->read_ptr == NULL)
  {
    return PARAM_RESULT_READ_ONLY;
  }

  out->kind = entry->kind;

  switch (entry->kind)
  {
    case PARAM_KIND_F32:
      out->f32 = param_load_f32(entry->read_ptr);
      break;
    case PARAM_KIND_I32:
      out->i32 = param_load_i32(entry->read_ptr);
      break;
    case PARAM_KIND_U32:
      out->u32 = param_load_u32(entry->read_ptr);
      break;
    case PARAM_KIND_U16:
      out->u16 = param_load_u16(entry->read_ptr);
      break;
    default:
      return PARAM_RESULT_UNKNOWN;
  }

  return PARAM_RESULT_OK;
}

param_result_t param_write_entry(const param_entry_t *entry, float value)
{
  if (entry == NULL)
  {
    return PARAM_RESULT_UNKNOWN;
  }

  return param_store(entry, value);
}
```

- [ ] **Step 3: 语法检查**

Run: 语法检查命令模板，`<files>` = `firmware/Src/param.c`
Expected: 无输出、退出码 0。

- [ ] **Step 4: 提交**

```bash
git add firmware/Inc/param.h firmware/Src/param.c
git commit -m "feat(param): 暴露参数表与按行读写接口"
```

---

## Task 4: `fibre_endpoints.c` 改为表分发层

**Files:**
- Modify: `firmware/Src/fibre_endpoints.c`（整体替换正文）
- Modify: `firmware/Inc/fibre_endpoints.h`（保留现有声明，无需改动）
- Test: 语法检查 + 一致性测试

- [ ] **Step 1: 重写 `fibre_endpoints.c`**

删除手写的 `s_endpoint_json` 与 16 路 switch，替换为：

```c
#include "fibre_endpoints.h"

#include "axis.h"
#include "config.h"
#include "fibre_endpoints_generated.h"
#include "param.h"

#include <string.h>

/*
 * Dispatch layer for the generated Fibre 0.1 endpoint tree.
 *
 * The endpoint ids, the JSON descriptor and the id -> s_params[] map all come
 * from fibre_endpoints_generated.h, which is generated from firmware/Src/param.c.
 * The only values handled here are the ones that have no param table row (the
 * board identity block) -- see fibre_identity_read().
 */

static uint16_t put_u8(uint8_t *buffer, uint16_t buffer_len, uint8_t value)
{
  if (buffer_len < 1u)
  {
    return 0u;
  }
  buffer[0] = value;
  return 1u;
}

static uint16_t put_u16(uint8_t *buffer, uint16_t buffer_len, uint16_t value)
{
  if (buffer_len < 2u)
  {
    return 0u;
  }
  buffer[0] = (uint8_t)(value & 0xFFu);
  buffer[1] = (uint8_t)((value >> 8) & 0xFFu);
  return 2u;
}

static uint16_t put_u32(uint8_t *buffer, uint16_t buffer_len, uint32_t value)
{
  if (buffer_len < 4u)
  {
    return 0u;
  }
  buffer[0] = (uint8_t)(value & 0xFFu);
  buffer[1] = (uint8_t)((value >> 8) & 0xFFu);
  buffer[2] = (uint8_t)((value >> 16) & 0xFFu);
  buffer[3] = (uint8_t)((value >> 24) & 0xFFu);
  return 4u;
}

static uint16_t put_u64(uint8_t *buffer, uint16_t buffer_len, uint64_t value)
{
  if (buffer_len < 8u)
  {
    return 0u;
  }
  for (uint32_t i = 0u; i < 8u; i++)
  {
    buffer[i] = (uint8_t)((value >> (8u * i)) & 0xFFu);
  }
  return 8u;
}

static uint16_t put_f32(uint8_t *buffer, uint16_t buffer_len, float value)
{
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return put_u32(buffer, buffer_len, bits);
}

/* Values with no param table row.  See IDENTITY_ENDPOINTS in
 * tools/gen_fibre_endpoints.py: these are marked FIBRE_EP_NOT_PARAM there. */
#define AT32_UID_WORD0   (*(volatile uint32_t *)0x1FFFF7E8u)
#define AT32_UID_WORD1   (*(volatile uint32_t *)0x1FFFF7ECu)

static uint16_t fibre_identity_read(uint16_t endpoint_id, uint8_t *buffer, uint16_t buffer_len)
{
  switch ((fibre_ep_id_t)endpoint_id)
  {
    case FIBRE_EP_FW_VERSION_REVISION:
      return put_u8(buffer, buffer_len, (uint8_t)FIRMWARE_VERSION_REVISION);
    case FIBRE_EP_HW_VERSION_MAJOR:
      return put_u8(buffer, buffer_len, 0u);
    case FIBRE_EP_HW_VERSION_MINOR:
      return put_u8(buffer, buffer_len, 0u);
    case FIBRE_EP_HW_VERSION_VARIANT:
      return put_u8(buffer, buffer_len, 0u);
    case FIBRE_EP_SERIAL_NUMBER:
      return put_u64(buffer, buffer_len,
                     ((uint64_t)AT32_UID_WORD0 << 32) | (uint64_t)AT32_UID_WORD1);
    default:
      return 0u;
  }
}
```

> 上面四个身份端点的声明 codec 是 `uint8`，所以写 **1 字节**；`serial_number` 是 `uint64`，写 8 字节。
> 宽度必须与生成头文件里 `s_fibre_ep_width[]` 的值一致——上位机按描述符里声明的宽度解析，
> 多写或少写都会让后续字段错位。

- [ ] **Step 2: 读/写分发**

```c
const char *fibre_endpoints_json(void)
{
  return s_fibre_json;
}

uint32_t fibre_endpoints_json_length(void)
{
  return FIBRE_JSON_LENGTH;
}

static const param_entry_t *row_for(uint16_t endpoint_id, uint32_t *count)
{
  const param_entry_t *entries = param_table_entries(count);
  uint16_t index;

  if ((endpoint_id == 0u) || (endpoint_id > FIBRE_EP_COUNT))
  {
    return NULL;
  }

  index = s_fibre_ep_index[endpoint_id];
  if ((index == FIBRE_EP_NOT_PARAM) || ((uint32_t)index >= *count))
  {
    return NULL;
  }

  return &entries[index];
}

uint16_t fibre_endpoint_read(uint16_t endpoint_id, uint8_t *buffer, uint16_t buffer_len)
{
  uint32_t count = 0u;
  const param_entry_t *entry = row_for(endpoint_id, &count);
  param_value_t value;

  if (entry == NULL)
  {
    return fibre_identity_read(endpoint_id, buffer, buffer_len);
  }

  if (param_read_entry(entry, &value) != PARAM_RESULT_OK)
  {
    return 0u;
  }

  switch (value.kind)
  {
    case PARAM_KIND_F32:
      return put_f32(buffer, buffer_len, value.f32);
    case PARAM_KIND_I32:
      return put_u32(buffer, buffer_len, (uint32_t)value.i32);
    case PARAM_KIND_U32:
      return put_u32(buffer, buffer_len, value.u32);
    case PARAM_KIND_U16:
      return put_u16(buffer, buffer_len, value.u16);
    default:
      return 0u;
  }
}

uint16_t fibre_endpoint_write(uint16_t endpoint_id, const uint8_t *buffer, uint16_t buffer_len)
{
  uint32_t count = 0u;
  const param_entry_t *entry;
  float value;

  if ((endpoint_id == 0u) || (endpoint_id > FIBRE_EP_COUNT))
  {
    return 0u;
  }

  /*
   * Dispatch on the kind, not on whether a table row exists: a function
   * endpoint has a row too (that is how clear_errors reuses the ASCII path),
   * but it must be called with an EMPTY payload instead of being written with a
   * codec-width value.
   */
  switch ((fibre_ep_kind_t)s_fibre_ep_kind[endpoint_id])
  {
    case FIBRE_EP_KIND_FUNCTION:
      return fibre_function_call(endpoint_id, buffer_len, row_for(endpoint_id, &count));

    case FIBRE_EP_KIND_IDENTITY:
      return 0u; /* identity endpoints are read-only */

    case FIBRE_EP_KIND_PROPERTY:
    default:
      break;
  }

  /*
   * A property is only written when the payload carries exactly its codec
   * width; the host sends an empty payload when it only wants to read, and an
   * empty payload must not be interpreted as "write zero".
   */
  entry = row_for(endpoint_id, &count);
  if (entry == NULL)
  {
    return 0u;
  }

  switch (entry->kind)
  {
    case PARAM_KIND_F32:
      if (buffer_len != 4u)
      {
        return 0u;
      }
      memcpy(&value, buffer, sizeof(value));
      break;
    case PARAM_KIND_I32:
      if (buffer_len != 4u)
      {
        return 0u;
      }
      value = (float)(int32_t)read_le32(buffer);
      break;
    case PARAM_KIND_U32:
      if (buffer_len != 4u)
      {
        return 0u;
      }
      value = (float)read_le32(buffer);
      break;
    case PARAM_KIND_U16:
      if (buffer_len != 2u)
      {
        return 0u;
      }
      value = (float)(uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8));
      break;
    default:
      return 0u;
  }

  if (param_write_entry(entry, value) != PARAM_RESULT_OK)
  {
    return 0u;
  }

  return buffer_len;
}
```

`read_le32()` 用 `fibre_server.c` 里同款的小端读取实现一份 static 的，或者从生成的宽度表推。
`fibre_function_call()` 是零参数函数分发：

```c
static uint16_t fibre_function_call(uint16_t endpoint_id, uint16_t payload_length,
                                    const param_entry_t *entry)
{
  /* Zero-argument functions: the host sends no payload and expects no value. */
  if (payload_length != 0u)
  {
    return 0u;
  }

  switch ((fibre_ep_id_t)endpoint_id)
  {
    case FIBRE_EP_SAVE_CONFIGURATION:
      /* Same entry point the ASCII 'ss' alias and the GUI both use. */
      (void)param_save_configuration();
      return 0u;

    case FIBRE_EP_AXIS0_CLEAR_ERRORS:
      /*
       * Do NOT add a second clear-errors implementation.  The real one is
       * param_on_write_clear_errors(), which is `static` in Src/param.c and is
       * reached through the `axis0.clear_errors` row's on_write hook.  So the
       * generator points s_fibre_ep_index[FIBRE_EP_AXIS0_CLEAR_ERRORS] at that
       * row and we simply write it -- identical to what the UART 'sc' command
       * does, including the return-to-IDLE behaviour.
       */
      (void)param_write_entry(entry, 1.0f);
      return 0u;

    default:
      return 0u;
  }
}
```

> **已核实**：`firmware/Inc/axis.h` 里**没有**清错误函数；真正的实现在 `firmware/Src/param.c:205`
> 的 `param_on_write_clear_errors`（`static`，外部拿不到）。所以函数端点必须复用
> `param_write_entry()`，不要新写一套清错误逻辑。相应地，`fibre_function_call()` 由调用方传入的
> `entry` 就是那一行，写一个非零值即可触发它的 `on_write`。
> `axis0.clear_errors` 那一行是 `config_ptr == NULL` 且 `on_write != NULL`，
> 所以 `param_store()` 的只读检查会通过、范围检查（`±PARAM_UNBOUNDED`、`clamp = true`）也会通过。

- [ ] **Step 3: 语法检查**

Run: 语法检查命令模板，`<files>` = `firmware/Src/fibre_endpoints.c`
Expected: 无输出、退出码 0。注意 AGENTS.md 要求 **0 warning**。

- [ ] **Step 4: 一致性测试仍然全绿**

Run: `python tools/fibre_conformance.py`
Expected: 全 PASS。

- [ ] **Step 5: 提交**

```bash
git add firmware/Src/fibre_endpoints.c
git commit -m "feat(fibre): 端点分发改为表驱动，新增函数端点与身份端点 provider"
```

---

## Task 5: `fibre_server.c` 的两处修正

**Files:**
- Modify: `firmware/Src/fibre_server.c`
- Test: 语法检查 + 一致性测试

- [ ] **Step 1: 无条件分发写载荷（否则零参数函数永远不执行）**

把 `fibre_server.c:111-114` 的：

```c
    if (payload_length > 0u)
    {
      (void)fibre_endpoint_write(endpoint_id, payload, payload_length);
    }
```

改为：

```c
    /*
     * Always offer the payload to the endpoint, even when it is empty: a
     * zero-argument function endpoint is called with a zero-length payload, so
     * gating this on payload_length > 0 would turn save_configuration() into a
     * silent no-op.  The endpoint itself decides whether the length is a write
     * (property, exact codec width) or a call (function, length 0).
     */
    (void)fibre_endpoint_write(endpoint_id, payload, payload_length);
```

- [ ] **Step 2: endpoint 0 的 `0xFFFFFFFF` 分支**

在 endpoint 0 的分支里，`offset` 解析之后、`offset < json_length` 判断之前插入：

```c
    if (offset == 0xFFFFFFFFu)
    {
      /*
       * Spec-defined sentinel: return the JSON version id instead of a slice of
       * the descriptor.  Kept for parity with the official endpoint0_handler()
       * even though today's clients do not send it.
       */
      uint32_t json_crc = (uint32_t)s_json_crc;
      uint32_t version_id = (json_crc << 16) |
                            (uint32_t)crc16_update((uint16_t)json_crc,
                                                   (const uint8_t *)json,
                                                   json_length);
      uint16_t written = 0u;

      if (output_length >= 4u)
      {
        s_tx_packet[2] = (uint8_t)(version_id & 0xFFu);
        s_tx_packet[3] = (uint8_t)((version_id >> 8) & 0xFFu);
        s_tx_packet[4] = (uint8_t)((version_id >> 16) & 0xFFu);
        s_tx_packet[5] = (uint8_t)((version_id >> 24) & 0xFFu);
        written = 4u;
      }

      response_length = (uint16_t)(2u + written);
      /* fall through to the send below -- do not return early */
    }
    else if ((offset < json_length) && (output_length > 0u))
```

> `s_json_crc` 是 `fibre_server.c` 里的 static 变量（第 13 行），endpoint 0 分支能直接访问。
> 需要确认 `crc16_update` 的返回类型与已有 include（`crc.h` 已在第 3 行包含）。
> **分支结构要小心**：原来的 `if ((offset < json_length) && (output_length > 0u))` 是 if/else 链的一环，
> 插入 `if/else if` 时不要破坏后面 `else` 分支的语义。实现后必须通读整个函数确认控制流。

- [ ] **Step 3: 语法检查**

Run: 语法检查命令模板，`<files>` = `firmware/Src/fibre_server.c`
Expected: 无输出、退出码 0。

- [ ] **Step 4: 在一致性测试里加断言**

在 `tools/fibre_conformance.py` 里补一条：模拟 `offset = 0xFFFFFFFF` 的请求，断言设备端口返回
4 字节，且其值等于 `(json_crc << 16) | crc16(json_crc, json)`。

Run: `python tools/fibre_conformance.py`
Expected: 全 PASS，包括新增的这一条。

- [ ] **Step 5: 提交**

```bash
git add firmware/Src/fibre_server.c tools/fibre_conformance.py
git commit -m "fix(fibre): 零参数函数端点不再被静默跳过；补齐 0xFFFFFFFF 版本 id 分支"
```

---

## Task 6: Makefile 的 `gen-fibre` / `check-fibre`

**Files:**
- Modify: `firmware/Makefile`
- Test: 直接运行等价的 python 命令（本机没有 make）

- [ ] **Step 1: 加目标**

在 `firmware/Makefile` 的 `.PHONY` 行里加入 `gen-fibre check-fibre`，并追加：

```make
# The Fibre descriptor is generated from the param table.  Regenerate after any
# change to s_params[] in Src/param.c, otherwise the host's json_crc trailer
# check silently rejects every request.
gen-fibre:
	python ../tools/gen_fibre_endpoints.py

check-fibre:
	python ../tools/gen_fibre_endpoints.py --check
```

- [ ] **Step 2: 用等价命令验证（本机无 make）**

Run: `python tools/gen_fibre_endpoints.py --check`
Expected: 退出码 0、无输出。

- [ ] **Step 3: 提交**

```bash
git add firmware/Makefile
git commit -m "build: 增加 gen-fibre / check-fibre 目标"
```

---

## Task 7: Keil 全量编译

**Files:** 无改动（除非编译报错）

- [ ] **Step 1: 跑 Keil 批处理编译**

**已实测通过的调用方式**（2026-10-11 在需求机上验证；注意三点：必须用**绝对路径**、日志名用
`keil_build.log` 才能命中 `.gitignore`、不要传 `-j0`）：

```powershell
$repo = "D:\1-myproject\AT32_ODrive\AT32_ODrive"
$proj = Join-Path $repo "firmware\AT32_ODrive.uvprojx"
$log  = Join-Path $repo "firmware\keil_build.log"
$p = Start-Process -FilePath "D:\Keil\UV4\UV4.exe" `
     -ArgumentList @('-b', $proj, '-o', $log) -Wait -PassThru -NoNewWindow
$p.ExitCode
Get-Content $log -Tail 60
```

Expected（基线实测输出，改动后仍须满足）：

```
*** Using Compiler 'V5.06 update 7 (build 960)', folder: 'D:\keil\ARM\ARMCC\Bin'
linking...
Program Size: Code=<...> RO-data=<...> RW-data=<...> ZI-data=<...>
".\build_keil\objects\AT32_ODrive.axf" - 0 Error(s), 0 Warning(s).
```

**基线（未改动的 commit 8d1b092）实测值**：`Code=57944 RO-data=7204 RW-data=460 ZI-data=5268`，
51 个编译单元，耗时约 3 秒，产物 `build_keil/objects/AT32_ODrive.axf`(726KB) + `.hex`(184KB)。

**已知环境坑（都踩过了，不要再重复）**：
1. **必须提权**：UV4 要写工作区外的 `D:\Keil` 与用户配置目录；在受限沙箱下会**静默失败**（不写日志、退出码 2），极易误判成工程有问题。
2. **必须在工作区外的写权限下用绝对路径调用**；相对路径的 `-o` 不会在预期位置产生日志。
3. 首次运行时 µVision 会把工程文件**规范化成 AC5**（新增 `<pArmCC>`、移除 `<RvdsCdeCp>`/`<nBranchProt>`、`<ClangAsOpt>4` → `<uClangAs>0`、删除 `<LayerInfo>`）。若之前因 `TOOLS.INI` 里失效的 `DEFAULT_ARMCC_VERSION_CM4="V5.06 update 3"` 而 abort，**再跑一次即可成功**——不需要改 `TOOLS.INI`，也不需要去 GUI 里手动选编译器。
4. 若日志里出现 `default ARM-Compiler version ... is not available` + `Build aborted`，就是第 3 条那个一次性规范化还没完成，重跑即可。

- [ ] **Step 2: 修复任何 error/warning**

AGENTS.md 要求 0 warning。常见来源：生成的 `s_fibre_json` 未被引用（检查 `fibre_endpoints_json()` 是否真的返回它）、
`fibre_identity_read` 的 `switch` 覆盖不全（补 `default`）、比较符号类型不匹配。

- [ ] **Step 3: 提交（若有修复）**

```bash
git add -A
git commit -m "fix(fibre): 修复 Keil 编译告警"
```

---

## Task 8: 文档同步

**Files:**
- Modify: `docs/odrive_mapping.md`（第 5 节）
- Modify: `docs/architecture.md`（§10 后续扩展点）
- Modify: `AGENTS.md`（验证清单 + 生成步骤）

- [ ] **Step 1: `docs/odrive_mapping.md` 第 5 节**

把"已实现最小 Fibre 0.1 USB 端点……且尚未上板验证"那段改为：端点树由 `param.c` 生成、
函数端点与身份端点的处置、以及**已验证到哪一步**（协议一致性测试 + Keil 编译通过；
上板验证仍待用户执行——**不要写成"已上板验证"**）。

- [ ] **Step 2: `docs/architecture.md` §10**

把"Fibre 0.1 端点的完整 0.5.x 对象树、订阅、函数端点和 DFU"改成剩余的未完成项
（订阅、DFU、复合类型端点），说明对象树与函数端点已完成、由 `param.c` 生成。

- [ ] **Step 3: `AGENTS.md`**

在"工作方式"的验证清单里加入：

```markdown
- 新增/修改 `firmware/Src/param.c` 的 `s_params[]` 之后，必须重跑
  `python tools/gen_fibre_endpoints.py` 并提交 `firmware/Inc/fibre_endpoints_generated.h`；
  漂移检查用 `python tools/gen_fibre_endpoints.py --check`。Fibre 描述符与参数表不一致时，
  上位机不会报错，只会静默拒绝所有请求。
- Fibre 描述符的一致性测试：`python tools/fibre_conformance.py`（不需要 C 工具链）。
```

- [ ] **Step 4: 提交**

```bash
git add AGENTS.md docs/odrive_mapping.md docs/architecture.md
git commit -m "docs: 同步 Fibre 端点树的实现与验证状态"
```

---

## 完成标准

1. `python tools/fibre_conformance.py` 全绿，包含 GUI Tuning 看板 13/13 路径与 `0xFFFFFFFF` 分支断言。
2. `python tools/gen_fibre_endpoints.py --check` 退出码 0。
3. 改动过的 .c 文件 `arm-none-eabi-gcc -fsyntax-only` 无输出。
4. Keil `0 Error(s), 0 Warning(s)`。
5. `git diff --stat` 不含 `tools/gen_keil_project.py`、不新增 `firmware/Src/*.c`。
6. 文档已同步，且**没有**把未做的上板验证写成已完成。
