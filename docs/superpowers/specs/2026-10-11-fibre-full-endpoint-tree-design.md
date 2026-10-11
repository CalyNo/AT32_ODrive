# Fibre 端点树补全（由 param.c 生成）设计

日期：2026-10-11
状态：已获用户批准（方案 A）；待用户审阅本文档
关联：`docs/odrive_mapping.md`、`docs/architecture.md` §10、`AGENTS.md`

---

## 1. 目标

让官方开源 ODrive GUI（`odriverobotics/ODrive` 仓库的 `GUI/`，Vue + fibre-js + Python 后端，走
Fibre 0.1）能够真正使用本板固件：不仅能**发现**设备，还能读出全部参数、绘制曲线、执行保存配置。

当前的 `firmware/Src/fibre_endpoints.c` 只手写了 16 个端点，GUI 的 Tuning 看板 13 个路径里有 7 个不存在，
配置页几乎全空。

## 2. 已核实的现状（全部有官方源码出处）

以下结论来自 `D:\1-myproject\AT32_ODrive\ODrive`（官方仓库克隆），不是推测：

| 项 | 官方出处 | 本固件 | 结论 |
| --- | --- | --- | --- |
| USB VID/PID | `cdc_desc.h:56-57` | `0x1209:0x0D32` | 一致 |
| 发现过滤器 | `GUI/fibre-js/fibre.js:601`、`GUI/server/odrive_server.py:75`、`tools/odrive/__init__.py:20` | `cdc_desc.c:236-238` vendor 接口 class 0/subclass 1/protocol 0 | 一致 |
| Fibre 端点 EP | ODrive v3.x 布局 | `EP 0x03` OUT / `EP 0x83` IN，64B | 一致 |
| trailer 语义 | `legacy_protocol.cpp:473` | `fibre_server.c:73` | 逐字一致 |
| CRC16 | `legacy_protocol.hpp:25` poly `0x3d65`；`docs/protocol.rst:104` init 为 `0x0001`（JSON CRC） | `crc.c:6` + `crc16_update(1u,...)` | 一致 |
| 类型字符串白名单 | `legacy_object_client.cpp:182-194` | 现用 `float/int32/uint32/uint8/uint64` | 一致（**必须是 `float`，不是 `float32`**） |
| 长描述符分片 | `legacy_object_client.cpp:368-374` 按实际收到字节 resize，收到 0 字节才停；61B 正是 ODrive 0.5.6+ `tx_mtu_ - 2`（`USB_TX_DATA_SIZE=64`）的值 | 单片上限 61B | **兼容**且与官方同值，描述符变大只是变多来回 |
| 官方 USB 协议代次 | 当前官方栈（含 WebUSB）在 USB 上一律构造 `LegacyProtocolPacketBased`；第二代分层协议只绑 CAN/CAN-FD | Fibre 0.1 | 目标代次正确 |
| 宿主 JSON 校验强度 | 无 schema、无必需端点；member 非法只记日志跳过；`serial_number` 缺失时回退 `[unknown serial number]` | 最小树 | 部分可用不会导致连接失败 |
| 函数端点 | `legacy_object_client.hpp:43-52`（首字段是 `ep_num`，**无 funid**）；`function_stubs_template.j2` 按端点 id 分发、直接解参/写返回 | 未实现 | 需新增 |
| GUI 版本门控 | `GUI/src/store.js:55-63` 仅按 `fw_version_minor` 选看板，其它值走默认分支 | `fw_version_minor = 1` | 不阻塞 |

字节级一致性已由上轮的一致性脚本实测（12/12 通过，`json_crc = 0xD851`）。

## 3. 范围

**做：**

- 由 `firmware/Src/param.c` 的 `s_params[]` 生成 Fibre 端点树（约 70 个属性），纳入构建与文档。
- 新增函数端点 `save_configuration`（GUI Tuning 看板唯一实际点击调用的函数）与 `axis0.clear_errors`。
- 补齐 spec 缺口：endpoint 0 的 `offset == 0xFFFFFFFF` 返回 `json_version_id`。
- 把一致性脚本固化进仓库，作为不需要工具链的回归测试。
- 同步 `docs/` 与 `AGENTS.md`。

**不做（YAGNI）：**

- 设备端订阅（GUI 的波形是**上位机轮询**采样，见 `GUI/src/store.js:80-85` 的 `sampledVarNames`，无需设备支持）。
- DFU、`erase_configuration`、`reboot`、`enter_dfu_mode`、`get_adc_voltage`、`test_function` 等 GUI 未调用的函数。
- 扩充 `param.c` 本身的参数集合（本次只把已有参数暴露到 Fibre）。
- 复合类型端点（`system_stats`、`oscilloscope` 等）。

## 4. 设计

### 4.1 生成器 `tools/gen_fibre_endpoints.py`

输入 `firmware/Src/param.c`，输出 `firmware/Inc/fibre_endpoints_generated.h`。

解析要点（这是本次最容易藏 bug 的地方，必须显式处理）：

- `s_params[]` 的行是**跨多行的**（例如 `axis0.motor.config.current_lim` 的 `PARAM_KIND_F32` 在下一行），
  因此**不能按行解析**：先截取 `s_params[] = { ... };` 的数组体，再按**顶层花括号分组**切出一行一行。
- 每行用一个针对该 C 子集的小词法器切成顶层逗号分隔的 token：字符串字面量、`NULL`、`&表达式`、
  `PARAM_KIND_*`、数字（含 `u`/`f` 后缀）。**不使用正则做启发式匹配。**
- 一行 = `path, alias, kind, precision, read_ptr, config_ptr, on_write, min, max, clamp`
  （`param.h:45-57`）。生成器只使用其中的 `path`、`kind`、`read_ptr`、`config_ptr`、`on_write`；
  `precision` 与 `min`/`max`/`clamp` 不参与 Fibre 协议（JSON 规范不允许注释，范围信息只写进
  生成头文件的 C 注释里）。
- `readable = (read_ptr != NULL)`；`writable = (config_ptr != NULL) || (on_write != NULL)`
  （与 `param.c:480-482` 判定 `PARAM_RESULT_READ_ONLY` 的判据同源）。
- **只使用规范路径 `path`，不使用 `alias`**（`alias` 是 UART 兼容拼写，`param.h:48`）。

**必须 fail loudly**，不允许静默降级：

- `kind` 不在白名单 → 非零退出并打印行号与路径。
- 解析出的行数与数组体的顶层分组数不一致 → 非零退出。
- 路径重复、或同一父子路径冲突（如既有 `a.b` 属性又有 `a.b.c` 属性）→ 非零退出。
- 任何一行 `read_ptr == NULL` 且无法归入函数端点 → 打印告警（当前唯一命中的是 `axis0.clear_errors`，见 4.5）。

生成器同时把 `json_crc` 与字节长度写进生成文件头部注释，便于与设备实测值对照。

### 4.2 类型与 access 映射

| `param_kind_t` | Fibre codec 字符串 | 线宽 |
| --- | --- | --- |
| `PARAM_KIND_F32` | `float` | 4 |
| `PARAM_KIND_I32` | `int32` | 4 |
| `PARAM_KIND_U32` | `uint32` | 4 |
| `PARAM_KIND_U16` | `uint16` | 2 |

`access` 为 `"rw"` 或 `"r"`。全部小端。

### 4.3 生成产物 `firmware/Inc/fibre_endpoints_generated.h`

只生成**头文件**，因此**完全不需要改动 `tools/gen_keil_project.py` 的 `USER_SOURCES`**，
从而不会引入 AGENTS.md 警告的 "Makefile 与 Keil 工程漂移"。

内容：

- `fibre_ep_id_t` 枚举：属性 id 按 `s_params[]` 表顺序从 1 连续分配；函数 id 在所有属性之后分配。
  枚举名由路径转大写加下划线（`AXIS0_CONTROLLER_CONFIG_POS_GAIN`），便于调试时对号。
- `s_fibre_json[]`：紧凑 JSON 字符串（无空白），按每个端点一个 C 字符串字面量分行拼接，便于阅读与 diff。
- `s_fibre_ep_index[]`：`id -> s_params[] 行索引` 的 `uint16_t` 表（函数端点用哨兵值）。
- `FIBRE_EP_COUNT`、`FIBRE_FUNCTION_COUNT`。
- 头部注释含生成命令、`param.c` 的内容哈希、行数、JSON 字节数、`json_crc`。

JSON 形状（对象节点不带 `id`，官方 `legacy_object_client.cpp:298-300` 的 object 分支不读 `id`）：

```json
[{"name":"fw_version_major","id":1,"type":"int32","access":"r"},
 {"name":"serial_number","id":74,"type":"uint64","access":"r"},
 {"name":"axis0","type":"object","members":[
   {"name":"controller","type":"object","members":[
     {"name":"config","type":"object","members":[
       {"name":"pos_gain","id":37,"type":"float","access":"rw"}]}]}]},
 {"name":"save_configuration","id":78,"type":"function","inputs":[],"outputs":[]}]
```

id 分配顺序：**① `param.c` 行（按表顺序）→ ② 显式身份表（见 4.3.1）→ ③ 函数端点**，全部连续无空洞。

生成的 `fibre_endpoints_generated.h` **入库提交**（Keil 是主工具链，不能要求先跑生成器），
并由 Makefile 与一致性测试检查其是否与 `param.c` 同步。

### 4.3.1 非 param.c 来源的身份端点（必须保留）

**param.c 并不是现有 16 个端点的完整来源。** 直接"只由 param.c 生成"会丢掉上位机识别板子
最需要的字段：

| 端点 | param.c 中是否存在 | 处置 |
| --- | --- | --- |
| `vbus_voltage` | 有（`param.c:191`） | 由 param.c 生成 |
| `fw_version_major` / `fw_version_minor` | 有（`param.c:187,189`，kind 为 `I32`） | 由 param.c 生成，codec 变 `int32` |
| `fw_version_revision` | **无** | 必须显式补充 |
| `hw_version_major` / `hw_version_minor` / `hw_version_variant` | **无** | 必须显式补充（值 0，表示"未知板"） |
| `serial_number` | **无** | 必须显式补充（AT32 UID） |

因此端点集合 = **`param.c` 行 ∪ 生成器内一张短小的显式表**（身份/派生端点）。

- 显式表给出这些端点的 `name`、`path`、codec、id，并标注"数值由 `fibre_endpoints.c` 里的
  手写 provider 提供"。
- 生成的 `s_fibre_ep_index[]` 用哨兵 `FIBRE_EP_NOT_PARAM` 标注这类端点；
  `fibre_endpoints.c` 的读分发起把哨兵交给一个手写的 `fibre_identity_read()`
  （内容就是现在 `fibre_endpoints.c` 里已有的那几行取 UID / 取版本号代码）。
- 生成头文件里必须带注释说明这些端点的数值来源不在 param.c，避免以后有人误以为它们由表驱动。

**宽度偏差是有意的、已文档化**：`fw_version_*` 由 `int32` 承载而非官方的 `uint8`，因为 param.c
是类型的唯一事实源。上位机是按**设备返回的 JSON** 建树的（`legacy_object_client.cpp:317-336`），
宽度差异对它不可见；`GUI/src/store.js:55-63` 用松散 `==` 比较版本号，两条分支都不受影响。
若以后确需与官方字节一致，只需在生成器里加一张 codec 覆盖表并让 `s_fibre_ep_codec[]` 参与
序列化——本次不做（YAGNI）。

### 4.4 `param.c` 新增接口（先在 `Inc/param.h` 定义）

Fibre 层需要"按行取值/写值"，而现有 `param_print()` 直接把结果打到 UART、`param_write_value()`
按字符串查找。新增三个接口，保持 param.c 是 kind→内存布局映射的唯一所有者：

```c
/* 读取一行的当前值，按 kind 填充联合体。*/
typedef struct
{
  param_kind_t kind;
  float        f32;
  int32_t      i32;
  uint32_t     u32;
  uint16_t     u16;
} param_value_t;

param_result_t param_read_entry(const param_entry_t *entry, param_value_t *out);

/* 规范参数表（只读），供 Fibre 按 id 索引，避免每帧做字符串查找。*/
const param_entry_t *param_table_entries(uint32_t *count);

/* 按行写入，与 param_write_value() 共用 param_store() 的全部检查与副作用。*/
param_result_t param_write_entry(const param_entry_t *entry, float value);
```

`param_write_entry()` 必须是 `param_store()` 的薄包装，**不得**复制任何范围检查、
`nvm_config_apply()` 下发或副作用逻辑。

### 4.5 `fibre_endpoints.c` 变为薄分发层

- `fibre_endpoints_json()` / `fibre_endpoints_json_length()` 直接返回生成的头文件里的字符串。
- `fibre_endpoint_read(id, buf, len)`：查 `s_fibre_ep_index[]` → `param_read_entry()` →
  按 `kind` 写 4/2 字节小端。
- `fibre_endpoint_write(id, buf, len)`：查表 → 解出 `float`（`float`/`int32`/`uint32`/`uint16`
  统一转成 `float`，与 CAN Simple 的做法一致）→ `param_write_entry()`。
- 函数端点：`save_configuration` → `param_save_configuration()`；
  `axis0.clear_errors` → 现有清错误路径（`param.c` 里 `axis0.clear_errors` 目前是只写 ASCII 属性，
  `read_ptr == NULL`，在 Fibre 里改成函数端点，所以它出现在函数表而不是属性表）。
- 生成表里的哨兵 `FIBRE_EP_NOT_PARAM`（身份端点，见 4.3.1）交给手写的 `fibre_identity_read()`。
- 未知 id、非法长度一律返回 0 字节（保持现有语义）。

### 4.6 补齐 spec 缺口

`fibre_server.c` 的 endpoint 0 分支增加：`offset == 0xFFFFFFFF` 时返回
`json_version_id = (json_crc << 16) | crc16(json_crc, json, len)`（官方 `endpoints_template.j2:34`）。
当前仓库内所有官方客户端都不发这个请求（`legacy_object_client.hpp:142` 的 `tx_buf_` 初值虽为
`0xFFFFFFFF`，但 `receive_more_json()` 会立刻覆盖它），成本 3 行，作为规范完整性补齐。

### 4.7 体积与时序预算

约 70 个属性 → JSON 估计 4–5 KB。USB 单片上限 61B → 约 80 个来回；`usb_fibre_poll()` 由 1ms
通信任务调用、每次处理一个包，故描述符整读约 80ms，远小于上位机重发超时。
`FIBRE_MAX_RESPONSE_DATA = 61` **不改**（63B 响应正好避开 64B 整包与 ZLP 问题）。

## 5. 安全

Fibre 写端点一律经 `param_write_entry()` → `param_store()`，因此自动继承：

- 范围检查与 `clamp`/拒绝语义（`param.c:491-498`）。
- 写持久化字段后立即 `nvm_config_apply()` 下发（`param.c:523-527`）。
- 改 `pole_pairs`/`cpr` 会作废编码器偏置校准并重新阻塞 `CLOSED_LOOP_CONTROL` 的保护。

这是复用 param.c 相比手写端点的最大安全收益。

`save_configuration` 只写内部 Flash，不改变使能状态；`axis0.clear_errors` 走既有的清错误路径
（会回到 IDLE），与 UART 的 `sc` 行为一致。

**红线不变**：仍然"未完成电流零偏校准前不得使能 PWM""未完成编码器方向/电角度校准前不得进入
`CLOSED_LOOP_CONTROL`"。本次改动**不新增**任何绕过这些保护的写路径。

## 6. 构建集成

- `firmware/Makefile`：加 `gen-fibre` 目标（跑生成器）与 `check-fibre` 目标（重新生成到临时文件并
  与入库版本对比，不一致则失败）。
- Keil：因为只生成头文件，`AT32_ODrive.uvprojx` 无需改动；`AGENTS.md` 里补一句"改了
  `param.c` 的 `s_params[]` 之后必须重跑生成器"。
- `tools/gen_keil_project.py` **不改**。

## 7. 验证

1. **一致性测试（无需工具链，可重复跑）**：把上轮的脚本固化到 `tools/fibre_conformance.py`，扩展为：
   - 由 `param.c` 独立重新生成一次期望描述符，与入库的生成头文件逐字节比对；
   - 描述符是合法 JSON、id 唯一且连续、每个类型都在官方 codecs 白名单内；
   - 用官方 `fibre.protocol.calc_crc16` 与其 `docs/protocol.rst` 测试向量校验 CRC；
   - 用固件包服务器的逐行端口驱动官方客户端的读描述符循环，断言完整取回且
     `json_crc` 与服务器期望值相等；
   - 断言 `GUI/src/assets/dashboards/Tuning_0_5_1.json` 的**全部 13 个路径**都能在树里解析到。
2. **语法检查**：`arm-none-eabi-gcc -fsyntax-only`（机器有 GCC 10.3.1，但无 `make`，跑不了 Makefile）。
3. **Keil 真机编译**：`D:\Keil\UV4\UV4.exe -b firmware\AT32_ODrive.uvprojx -o <log>`，要求 0 error 0 warning。
4. **人工对照**：`docs/odrive_mapping.md` 第 5 节关于 Fibre 的段落改为"已验证"结论；
   `docs/architecture.md` §10 移除已完成的条目。

## 8. 未决 / 风险

| 风险 | 说明 | 处置 |
| --- | --- | --- |
| 生成器解析失手 | 静默解析错会让描述符 CRC 与设备不一致，现象是"上位机什么都没发生" | 生成器 fail loudly + `check-fibre` 目标 + 一致性测试 |
| 描述符变大 | ~80 个来回 | 已确认官方客户端吃分片；实测若慢再考虑提高单片上限 |
| 无法上板验证 | 本机无板 | 本次交付以 Keil 编译 + 一致性测试为证据；上板由用户执行 |
| Keil 链接 | AGENTS.md 提到 Keil 不做 `--gc-sections`，未调用的库钩子符号也必须存在 | 本次不新增源文件、只是新增对既有符号的调用，风险低；仍跑 Keil 全量编译确认 |

## 9. 验收标准

1. `tools/fibre_conformance.py` 全绿，且断言 GUI Tuning 看板 13/13 路径存在。
2. 生成描述符中 `hw_version_major/minor/variant`、`serial_number`、`fw_version_revision` **仍然存在**
   （未被 param.c 生成流程吞掉，见 4.3.1）。
3. `arm-none-eabi-gcc -fsyntax-only` 对改动文件无输出。
4. Keil 全量编译 0 error 0 warning。
5. `git diff` 不包含对 `tools/gen_keil_project.py` 的改动、不新增 `firmware/Src/*.c`。
6. `docs/` 与 `AGENTS.md` 已同步。
