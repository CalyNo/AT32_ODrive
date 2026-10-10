# 上板验证清单（无电机）

本文件给出**手头没有电机时**也能执行的整板验证步骤。所有命令都是 UART3（`PB10=TX`,
`PB11=RX`，**115200 8N1**，行尾 `\r\n`）上的 ODrive 风格 ASCII 协议：

```text
r <path>            读参数，只回数值
w <path> <value>    写参数，成功静默
ss                  保存到 flash         sr  重启        sc  清错误
p/v/c/t/f/u         位置/速度/力矩/梯形轨迹/反馈/喂通信看门狗
```

固件启动会打印（`BOOT_TRACE_ENABLE=1` 时）：

```text
boot: reset=POR (0x02) clock=HEXT+PLL 288MHz
boot: clock+gpio+adc ok
boot: config loaded           ← 或 defaults（首次上电/校验失败）
AT32_ODrive 0.1.0 ready
```

## 0. 串口调试速查

行尾 `\r\n`；`r <path>` **只回数值**（无 `path: ` 前缀，方便上位机 `toFloat()`）；
`w <path> <value>` **成功不回任何东西**。**任何一行命令都会喂通信看门狗**。

命令字母（与 ODrive 同名同义，单轴 `<m>`=0）：

| 命令 | 作用 |
| --- | --- |
| `r <path>` | 读参数 |
| `w <path> <value>` | 写参数 |
| `p <m> <pos> [vel_ff] [torque_ff]` | 位置模式 + 设定点 |
| `v <m> <vel> [torque_ff]` | 速度模式 |
| `c <m> <current>` | 力矩模式（单位 A） |
| `t <m> <destination>` | 梯形轨迹移动 |
| `f <m>` | 反馈，回 `pos vel` |
| `u <m>` | 喂通信看门狗 |
| `ss` / `sr` / `sc` | 保存配置 / 软复位 / 清错误（`se` 擦除未实现） |

错误回复：`invalid property`（路径不存在）、`invalid value`（越界）、
`invalid command format`（缺参数）、`invalid motor <n>`、`not implemented`、
`unknown command`、`save failed`。

按排查目的分类的读命令：

**启动 / 时钟 / 看门狗**

| 路径 | 含义 |
| --- | --- |
| `board.reset_cause` | 本次上电的复位原因位掩码（bit0 NRST、bit1 POR、bit2 SW、bit3 **IWDG**、bit4 WWDG、bit5 LPRST） |
| `board.clock_degraded` | 1 = HEXT 没起来，跑在内部 HICK |
| `board.core_clock` | 系统时钟 Hz，正常应为 `288000000` |
| `fw_version_major` / `_minor` | 固件版本 |
| `axis0.last_error` / `axis0.last_error_time_ms` | 最近一次错误码与时刻 |

**电源 / 温度 / 电流采样**

| 路径 | 含义 |
| --- | --- |
| `vbus_voltage` | 母线电压（分压 1/11） |
| `axis0.motor.fet_thermistor.raw` / `.temperature` | PB1 板载 NTC |
| `axis0.motor.motor_thermistor.raw` / `.temperature` | PB0 外部温度输入 |
| `adc.current_raw_a` / `_b` / `_c` | 三路电流采样原始值（零电流 ≈2048） |
| `adc.current_offset_a` / `_b` / `_c` | 上电零偏校准得到的偏置误差（V） |

**编码器 / 状态机 / 控制量**

| 路径 | 含义 |
| --- | --- |
| `axis0.encoder.raw` / `.shadow_count` / `.pos_estimate` / `.vel_estimate` | MT6816 原始帧/计数/位置/速度 |
| `axis0.current_state` / `axis0.requested_state` | 状态机（1=IDLE、8=CLOSED_LOOP、15=ERROR） |
| `axis0.error` | 错误位掩码（可写：`w axis0.error 0` 清） |
| `axis0.motor.phase_resistance` / `phase_inductance` | 校准结果 |
| `axis0.motor.current_control.Id_setpoint` / `Iq_setpoint` | 电流给定 |
| `axis0.controller.input_pos` / `input_vel` / `input_torque` | 输入设定点 |

**状态灯**

| 命令/路径 | 含义 |
| --- | --- |
| `w led.self_test 1` | 红→绿→蓝→白→灭 自检循环（`0` 关闭） |
| `w led.mode 1` + `w led.color 0xRRGGBB`（十进制）+ `w led.brightness 0..255` | 固定颜色 |
| `led.busy` / `led.frames` / `led.frames_done` | 帧数/完成数诊断（见第 6 节） |

**CAN / 配置持久化**

| 命令/路径 | 含义 |
| --- | --- |
| `can.termination` | 板上 120Ω 终端开关（`w can.termination 1` 打开） |
| `axis0.can.node_id` | CAN 节点号（写入后 `ss`） |
| `can.config.baud_rate` | 波特率（默认 250000，`ss`+`sr` 后生效） |
| `axis0.config.can.heartbeat_rate_ms` | 心跳周期 |
| `ss` / `sr` | 保存 / 重启 |

> 注意：`led.*`、`adc.*`、`can.termination`、`board.*` 都是**运行时**参数，
> `ss` 不会保存它们；`w axis0.requested_state 8`（闭环）在未完成校准前会被拒绝并置
> `AXIS_ERROR_INVALID_STATE(1)`。

## 0. 准备工作

| 工具 | 用途 |
| --- | --- |
| USB-TTL（3.3V） | 串口验证、参数读写 |
| 万用表 | 母线电压、3.3V/5V 电源、CAN 终端电阻 |
| USB-CAN 适配器（可选） | CAN Simple 验证 |
| 示波器/逻辑分析仪（可选） | PB2 波形、PWM 时序 |
| 限流电源 | 上电前先限流到 0.3-0.5A（没有异常再放开） |

上电前：确认 3.3V 对地不短路；`PC13` 对应的 CAN 终端开关默认关断；`BOOT0` 有下拉。

## 1. 串口与系统时钟

```text
r fw_version_major      -> 0
r fw_version_minor      -> 1
```

- 能收到 `ready` 且数值正确 → 288MHz 时钟（HEXT bypass + PLL）与 UART 分频正常
  （波特率由系统时钟推导，时钟不对会直接表现为乱码）。
- **完全无输出**是最需要区分的两种情况，第一行 `boot:` 会直接告诉你：
  - 出现 `clock=DEGRADED(HICK)`：8MHz 有源晶振（X2，PH0）没起来，固件退回内部 HICK 跑起来了。
    用示波器测 PH0 是否有 8MHz 方波；没有就查 X2 供电、OE 脚、R90。此时**功率级被锁死**
    （`pwm_enable()` 拒绝使能、`axis_arm()` 报 INVALID_STATE），属预期保护。
  - 依然一个字符都没有：说明连 HICK 兜底都没走到 —— 大概率是主机侧问题（TX/RX 接反、
    没共地、适配器是 5V 电平、选错 COM 口），其次才是复位/供电。用调试器看 PC 停在哪，
    参考 `docs/assumptions.md` 第 7、13 节。
- 收到乱码 → 波特率不对（应 115200）或时钟分频不对。
- 历史坑（已修复，见 `docs/assumptions.md` 第 14 节）：`uart_comm_init()` 曾经漏掉
  `usart_transmitter_enable()`/`usart_receiver_enable()`，表现为 PC 停在
  `board_uart_write()` 的 TDBE 等待里、整板完全静默。现在收发器已使能，且该等待有界。

## 2. 复位原因与看门狗

```text
boot: reset=... 这一行就是证据
```

| `reset=` | 含义 |
| --- | --- |
| `POR` | 上电复位（正常） |
| `NRST` | 复位脚/调试器复位 |
| `SW` | 软件复位（`sr` 命令） |
| `IWDG` | 看门狗超时复位 → 运行期间有地方超时 200ms |

验证 IWDG 真的有效：调试器里让 CPU **halt 超过 200ms**，再运行；板子应当被复位，
重启后 `boot: reset=IWDG`。这条同时也说明：调试时长时间 halt 会被看门狗打断，属于正常现象。
（IWDG 在 `app_init` 末尾才使能，启动阶段卡住不会被它掩盖成复位循环。）

## 3. 母线电压与温度

```text
r vbus_voltage                          -> 与万用表读数 × 1 一致（分压比 1/11）
r axis0.motor.fet_thermistor.raw        -> PB1 板载 NTC 的 12 位原始值
r axis0.motor.fet_thermistor.temperature-> ≈ 室温（10k/B3950 + 3.3k）
r axis0.motor.motor_thermistor.raw      -> PB0 外部温度输入（未接时可能落在窗口外）
```

- NTC 原始值应在 `TEMP_NTC_RAW_MIN/MAX_COUNTS`(33..3862) 之内，否则固件判定传感器失效
  （`axis0.error` 会带 `AXIS_ERROR_TEMPERATURE_SENSOR_FAILED = 4096`）。
- 用吹风机/手指加热 NTC，`temperature` 应上升。

## 4. 电流采样链路（零电流）

上电时固件自动做 256 次零电流采样（`adc_calibrate_current_offsets()`），结果可读：

```text
r adc.current_raw_a         -> 期望 ≈ 2048（1.65V 偏置 / 3.3V / 12bit）
r adc.current_raw_b         -> 同上
r adc.current_raw_c         -> 同上
r adc.current_offset_a      -> 期望 |offset| < 0.02 V（偏置误差）
r adc.current_offset_b
r adc.current_offset_c
```

判据与换算（2mΩ 低边 + RS724 增益 50 → 0.1 V/A）：

- 三相 raw 都在 2048 附近、且**互差小于 ~50 counts** → 偏置/增益/ADC 通路正常。
- raw 卡在 0 或 4095、或某相明显偏离 → 该相采样网络（分流电阻、放大器、ADC 引脚）有问题。
- 换算：`电流(A) ≈ (2048 - raw) / 124`；raw 大于 2048 表示反向电流符号。
- 注意：PWM 未使能时不会有新的注入转换，所以这三个 raw 值是上电校准时刻的快照，
  正好就是"零电流"状态。要验证带电流的情况，见第 9 节（需要电机）。

## 5. 编码器（MT6816，SPI3）

```text
r axis0.encoder.raw            -> 16 位原始帧：bit0 = 奇偶校验, bit1 = 磁警告, bit15..2 = 14 位角度
r axis0.encoder.shadow_count   -> 多圈计数
r axis0.encoder.pos_estimate   -> 位置估计（转/单位）
r axis0.encoder.vel_estimate   -> 速度估计
```

- 手边有磁性物体（小磁铁）时，在编码器芯片上方转动：`raw` 的 14 位角度应连续变化，
  `shadow_count` / `pos_estimate` 跟随变化。
- 没有磁铁时 `raw` 的 bit1（磁警告）可能置位，属正常。
- 编码器连续读失败会闭锁 `AXIS_ERROR_ENCODER_FAILED(64)` 并关断 PWM。

## 6. 状态灯（WS2812B-2020 / PB2）

三种验证手段，从简到繁：

**a. 自检序列**（不需要电机、不需要示波器）

```text
w led.self_test 1     -> 红 → 绿 → 蓝 → 白 → 灭，每 500ms 一步，循环
w led.self_test 0     -> 回到自动状态指示
```

**b. 直接指定颜色**

```text
w led.mode 1
w led.color 16777215  -> 0xFFFFFF 白（最亮，先确认能亮）
w led.color 16711680  -> 红
w led.color 65280     -> 绿
w led.color 255       -> 蓝
w led.brightness 255  -> 亮度拉满（默认 64 = 25%）
w led.mode 0          -> 回到自动（空闲=蓝、闭环=绿、错误=红闪、校准=蓝闪）
```

**c. 自动状态指示**（自动模式下不需要电机就能看到的）

```text
w axis0.error 1       -> 红色闪 1 次后停顿（错误码 = 最低置位位序号+1）
w axis0.clear_errors 1
```

**d. 如果还是不亮：用计数器定位**

```text
r led.busy          -> 0/1
r led.frames        -> 已启动的帧数
r led.frames_done   -> DMA 传输完成（帧真正发完）的次数
```

| `led.frames` | `led.frames_done` | 结论 |
| --- | --- | --- |
| 一直不涨 | 一直不涨 | `status_led_task` 没跑，或颜色一直没变化 → 查调度与内核状态 |
| 涨，但 `frames_done` 停在后面且 `led.busy=1` | 不涨 | **固件侧问题**：TMR20/DMA 没产出波形（查 DMAMUX 请求号、TMR20 时钟、CC1 事件） |
| 两者同步增长 | 同步 | 波形已经发出 → 问题在 **LED 供电/接线/电平**（下一步） |

**e. 示波器（最权威）**：探头接 PB2，自检模式下应看到

- 比特周期 1.25µs；`0` 位高电平 0.35µs，`1` 位高电平 0.70µs；
- 每帧 24 位后 ≥50µs 低电平（复位/锁存），实测帧间隔 500ms（自检）或 100ms（自动模式）。

**f. 硬件注意点**（波形正常但灯不亮的常见原因）

- LED 的 VDD 有没有接（3.3V 或 5V），以及是否与固件假设一致；
- 若 LED 由 **5V** 供电：WS2812B 的 VIH 约 `0.7 × VDD = 3.5V`，3.3V 数据可能不够，
  表现为不亮或随机颜色 —— 需要电平转换（或让 LED 用 3.3V 供电）；
- DIN/DOUT 是否接反（DIN 应接 PB2），数据线上是否有串联电阻；
- 3.3V 电源带载能力（白 25% 三通道全开时电流会明显上升，自检的白色步骤就是压力测试）。

## 7. CAN（需要 USB-CAN 适配器）

```text
w can.termination 1     -> 打开板上 120Ω 终端（短线/单适配器时必须）
r axis0.can.node_id     -> 默认 0
r can.config.baud_rate  -> 默认 250000（改后需 ss + 重启才生效）
ss                      -> 保存配置（成功静默，失败回 save failed）
```

- 上电后应每 100ms 收到 ID = `node_id << 5 | 0x01` 的心跳（node 0 → `0x001`），
  字段布局与 `odrive-cansimple.dbc` 一致。
- 用 `tools/odrive_can_smoke.py`（python-can + 官方 DBC）解码心跳/母线电压/编码器估计。
- 收不到：确认 `can.termination`、适配器波特率 250k、PB8/PB9 与 SIT3051 的 TXD/RXD 方向、
  120Ω 是否重复（两端各一个）。

## 8. Flash 配置与参数持久化

```text
w can.config.baud_rate 500000     -> 写入（立即生效于 RAM，CAN 重启后生效）
ss                                -> 保存（静默=成功）
sr                                -> 重启
boot: config loaded               -> 说明 CRC/version 校验通过
r can.config.baud_rate            -> 500000
```

- `boot: config defaults` 表示 flash 里的镜像无效（首次上电或 CRC 失败）→ 固件用默认值并重写。
- 参数表里 `led.*`、`adc.*`、`can.termination` 都是**运行时属性**，`ss` 不保存它们。

## 9. 现在（无电机）**验证不了**的部分

这些必须在接上电机 + 限流电源后做，原因是固件按 `AGENTS.md` 的安全红线设计，
不允许在未校准前使能功率级：

| 项目 | 依赖 |
| --- | --- |
| PWM 波形/死区/极性、相序 | 需要正常状态机进入使能（`MOTOR_CALIBRATION`/`CLOSED_LOOP_CONTROL`） |
| 电流环、功率限制、过流保护 | 需要真实电流 |
| 编码器偏置/方向校准、`CLOSED_LOOP_CONTROL` | 需要电机 + MT6816 磁铁 |
| 力矩常数、速度/位置增益整定 | 需要电机 |
| 制动斩波 | 本板没有该硬件，禁止 |

可以先做的"故障路径"验证（不需要电机）：

```text
w axis0.error 64          -> 演示编码器故障：PWM 保持关断、`axis0.current_state` 仍为 1(IDLE)、红灯闪 7 次
w axis0.clear_errors 1
w axis0.requested_state 8 -> 未校准时被拒绝，`axis0.error` 置 INVALID_STATE(1)、红灯闪 1 次
w axis0.clear_errors 1
```

## 10. USB CDC 虚拟串口

USB FS 设备（`PA11 = DM`、`PA12 = DP`，OTGFS1）现在实现了 **CDC-ACM 虚拟串口**，插上电脑
出现一个 COM 口，**说完全相同的 ASCII 协议**（`r`/`w`/`ss`/`sr`/`sc`…），参数表、启动 trace、
状态灯自检等全部可用 —— 和 `PB10/PB11` 那条串口是两个独立主机口，任何一个都能独立操作。

```text
r usb.connected     1 = 主机已枚举并配置了 CDC 接口
r usb.rx_bytes      主机发到板子的字节数（调试用）
r usb.tx_dropped    TX 环形缓冲溢出丢字节数（正常应长期为 0）
```

要点与前置条件：

- **必须 HEXT 正常**：OTGFS 需要精确 48MHz，本板取 288MHz / 6；`board.clock_degraded = 1`
  （跑在 HICK）时 USB 根本不启动（`usb.connected` 恒 0），因为 ±3% 的时钟无法枚举。
- **不做 VBUS 检测**（`USB_VBUS_IGNORE`）：OTG 的 VBUS 引脚在 AT32 上固定是 `PA9`，而本板
  `PA9 = PWM_B_H`，所以只能关闭 VBUS 检测；CDC 作为总线供电设备正常工作，代价是无法感知
  拔插瞬间的 VBUS 状态。
- 端口参数（波特率/流控）对虚拟串口没有意义，随便设；两端都是 8N1、`
`。
- TX 是**广播**的：串口和 USB 会同时收到所有回复（启动 trace、`f`/`r` 回显），方便同时接
  两个终端对比。
- 排查顺序：插上后设备管理器没有新设备 → 先看 `r usb.connected`、`r board.clock_degraded`；
  设备出现但打开失败 → 换 USB 线（很多线只有电源线）、确认 `PA11/PA12` 焊接与 D+/D- 没接反；
  能打开但没数据 → 用 `w led.self_test 1` 之类会产生输出的命令，并看 `r usb.tx_dropped`。
- **仍未实现**：ODrive 原生 USB 协议（Fibre + DFU，`odrivetool` 用的那套），要走那条路需要
  在 CDC 之上再实现二进制协议。

## 11. 现成的上位机脚本

```text
tools/odrive_ascii_smoke.py    # pyserial：校验 ASCII 响应语义（只回数值、写成功静默等）
tools/odrive_can_smoke.py      # python-can + cantools：用官方 DBC 解码心跳/编码器/母线
```

两者都是 bring-up 辅助，未在本板上运行过（见 `docs/assumptions.md`）。
