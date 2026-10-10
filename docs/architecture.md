# 固件架构

## 1. 分层

```text
应用层
  ├─ app.c                    初始化顺序 + 任务注册
  ├─ scheduler.c              1ms 协作式任务调度
  └─ system_time.c            SysTick 毫秒/DWT 微秒/周期时间基准

通信/状态层
  ├─ uart_comm.c              主机链路：ASCII 行协议 + 收发缓冲（UART3/USB 共用）
  ├─ usb_cdc.c                USB FS CDC-ACM 虚拟串口（OTGFS1）
  ├─ param.c                  参数表：路径 -> 类型/范围/存储/副作用
  ├─ can_comm.c               CAN Simple 子集
  ├─ status_led.c             状态灯任务（读轴状态 -> 颜色，串口/CAN 可覆盖）
  ├─ led_pattern.c            纯逻辑：状态/错误 -> 颜色图案 + WS2812 帧编码
  ├─ ws2812.c                 WS2812B 驱动（TMR20_CH1 + DMA1_CH1，非阻塞）
  └─ nvm_config.c             Flash 参数持久化 + 配置下发（nvm_config_apply）

轴与控制器层
  ├─ axis.c                   状态机、保护、输入输出
  └─ controller.c             力矩/速度/位置控制 + TrapTraj + 电流 PI

电机控制核心（无硬件依赖，可在主机侧测试）
  ├─ foc.c                    Clarke/Park/反Park/SVPWM
  ├─ pid.c                    PI 控制器
  ├─ traptraj.c               梯形轨迹规划
  ├─ encoder_pll.c            编码器 PLL、位置/速度/电角度估计
  ├─ crc.c                    CRC-32（配置镜像校验）
  └─ util.c                   clamp / 环绕等纯函数

硬件抽象层
  ├─ board.c                  时钟、GPIO、看门狗、SPI、UART、温度换算
  ├─ pwm.c                    TMR1 互补 PWM
  ├─ encoder.c                MT6816 / SPI1 编码器 SPI 传输
  ├─ adc.c                    ADC1 注入组（电流/母线）+ 规则组（温度）
  └─ irq_priority.h           中断抢占优先级策略（唯一来源）
```

分层规则：

- 标注"无硬件依赖"的模块只允许包含 `stdint/stdbool/math/string` 和 `Inc/` 下的纯头文件，
  `tests/Makefile` 直接编译它们，因此不得引入 CMSIS、寄存器或全局硬件状态。
- 参数只能通过 `param.c` 的参数表暴露，ASCII 与 CAN 两条协议共用同一张表，
  避免同一个参数在两个协议里各写一遍（早期版本在 `uart_comm.c` 里维护了两条 100+ 行的
  if/else 链，读写路径容易漂移）。


## 2. 实时结构

| 任务 | 频率 | 触发源 | 处理内容 |
| --- | --- | --- | --- |
| 电流环 | 24kHz | TMR1_CH4 -> ADC 注入完成中断 | 电流采样、Clarke/Park、功率限制、电流 PI、前馈/解耦、SVPWM |
| 速度/位置环 | 8kHz | TMR2 溢出中断 | 读编码器、电角度、速度/位置控制、状态机和保护 |
| 系统任务 | 1ms | `scheduler.c` | 喂 IWDG、通信超时看门狗 |
| 通信任务 | 1ms | `scheduler.c` | UART 解析、CAN 收发、周期心跳/上报 |
| 校准任务 | 1ms | `scheduler.c` | 有校准请求时执行阻塞式 R/L 或编码器偏置校准 |
| 温度采样 | 100Hz | `scheduler.c`（1ms 任务内分频） | ADC1 规则组软件触发采样 PB0/PB1，更新 `temp_motor`/`temp_mos`，含传感器失效检测 |
| 状态灯任务 | 100ms | `scheduler.c` | 采样轴状态/错误，状态变化时启动一帧 WS2812B（`ws2812_write` 只登记 DMA，不阻塞） |

`main.c` 只负责 `app_init()` 和 `app_run()`，不再直接调用外设或 WS2812。

### 2.0 启动顺序与 IWDG

`app_init()` 的顺序是刻意的，不要随意把 `board_watchdog_init()` 往前挪：

1. `board_reset_cause_take()` —— 读并清 CRM 复位标志（复位原因要能解释）；
2. `board_watchdog_feed()` —— 重新装载上一次运行遗留的 IWDG 计数器；
3. `board_clock_config()` → `board_init()` → `uart_comm_init()`（UART 提前，启动路径才能打点）；
4. PWM/ADC 初始化与电流零偏校准 → 配置加载（必要时一次性写入 flash）；
5. `axis_init()` / `nvm_config_apply()` / CAN / 状态灯 / TMR2 / 调度器任务注册；
6. **最后**才 `board_watchdog_init(200u)` 真正使能 IWDG，随后由 1ms 系统任务喂狗。

原因：IWDG 由 LICK 独立计数，系统复位不会让它停止，一旦使能也无法关闭。若在步骤 4/5 之前
就使能它，任何一次卡住（等 HEXT/PLL、等 ADC 标志、等 UART TX、flash 擦写）都会在 200ms 后
变成复位 → 再进 `app_init` → 再卡，永远看不到真正的卡点。详见 `docs/assumptions.md` 第 13 节。

`BOOT_TRACE_ENABLE`（`Inc/config.h`，默认开）会打印复位原因和每个阶段的进展，
便于用串口定位启动卡点；稳定后置 0。

`board_clock_config()` 中 HEXT/ PLL 的等待都是有界的：8MHz 有源晶振没起来时退到内部 HICK
（同参数 PLL 仍约 288MHz），并在第一行打印 `clock=DEGRADED(HICK)`。降级运行时功率级被锁死
（`pwm_enable()` 直接返回、`axis_arm()` 报 `AXIS_ERROR_INVALID_STATE`），因为 PWM 频率、死区、
ADC 采样点和电流环周期都由系统时钟推导。见 `docs/assumptions.md` 第 7 节。

中断优先级（数值越小优先级越高），唯一来源是 `Inc/irq_priority.h`，本表必须与其保持一致：

| 中断 | 优先级宏 | 值 |
| --- | --- | --- |
| ADC1_2_3_IRQn（电流环） | `IRQ_PRIORITY_CURRENT_LOOP` | 1 |
| USART3_IRQn | `IRQ_PRIORITY_COMMUNICATION` | 2 |
| CAN1_RX0_IRQn | `IRQ_PRIORITY_COMMUNICATION` | 2 |
| TMR2_GLOBAL_IRQn（速度/位置环） | `IRQ_PRIORITY_CONTROL_LOOP` | 3 |
| USB 与 UART 是两个独立主机口，协议/参数表完全相同（`uart_comm_write()` 广播到两边） | | |
| OTGFS1_IRQn（USB FS） | `IRQ_PRIORITY_USB` | 2 |
| DMA1_Channel1_IRQn（状态灯帧尾） | `IRQ_PRIORITY_STATUS_LED` | 4 |

### 2.1 状态指示（WS2812B-2020 / PB2）

`PB2 = TMR20_CH1`（`GPIO_MUX_2`），TMR20 走 APB2 定时器时钟 288MHz，一个 PWM 周期
= 1.25µs（ARR=359）；`T0H=100`、`T1H=201` tick。帧由 DMA1_CH1 按"每比特一次传输"送出：
通道 1 的比较事件把下一位写入 `C1DT` 缓冲寄存器，溢出事件再搬进活动寄存器，因此 CPU
只在启动帧时写几个寄存器（旧实现用关中断位翻转，每帧会挡住中断约 90µs）。
`led_pattern.c` 里的 `led_ws2812_encode()` 负责 G-R-B / MSB-first 的帧编码，可主机侧测试。

帧尾（第 25 个 entry）的 DMA 传输完成中断只做三件事：停计数器、关 DMA 通道、清标志。
即使这个中断被高优先级中断（如 24kHz 电流环）拖后，帧尾槽的 0 也会在下一个溢出事件
自动搬进活动寄存器，数据线保持低电平直到下一帧，因此 **不会多发出一位**，同时天然满足
WS2812 的复位/锁存时间。

颜色与图案（`led_pattern.c`，`led.mode = 0` 时的行为）：

| 条件 | 指示 |
| --- | --- |
| `error != 0` | 红色闪 N 次后停顿；N = 最低置位错误位序号+1（见 `led_pattern.h` 表） |
| 校准/启动序列进行中 | 蓝色 400ms 亮 / 400ms 灭 |
| `CLOSED_LOOP_CONTROL` | 绿色常亮 |
| 其它（空闲/掉使能） | 蓝色常亮 |

亮度默认 25%（`STATUS_LED_DEFAULT_BRIGHTNESS = 64`），由 `led.brightness` 缩放；
`led.mode = 1` 时用 `led.color`（0xRRGGBB）常亮，`led.mode = 2` 熄灭。
这三个设置存在 `g_status_led_*`（`status_led.c`）里，**只作用于运行时**，不进
`odrive_config_t`、不写 flash：持久化要动 NVM 布局并顶 `NVM_CONFIG_VERSION`，会把已保存的
电机/编码器校准一起作废（详见 `docs/assumptions.md` 第 12 节）。

## 3. 电流环数据流

```text
ADC injected: ia, ib, ic, vbus
  -> 根据 encoder.electrical_angle 做 Clarke + Park
  -> i_d / i_q
  -> DC bus 功率限制（上一周期功率估计）
  -> controller_update_current_references()
       -> i_d_pid, i_q_pid
  -> R/L 前馈 + d/q 解耦 + bEMF 前馈
  -> 电压圆限制
  -> 反 Park -> v_alpha / v_beta
  -> foc_svpwm(vbus) -> duty_a/b/c
  -> pwm_set_duty()
  -> i_bus / power 估计（供下一周期限流）
```

## 4. 轴状态机

当前支持：

- `AXIS_STATE_IDLE`：PWM 关断，控制器复位。
- `AXIS_STATE_MOTOR_CALIBRATION`：阻塞式测量相电阻和相电感。
- `AXIS_STATE_ENCODER_OFFSET_CALIBRATION`：开环旋转电角度并线性拟合编码器方向与偏置。
- `AXIS_STATE_FULL_CALIBRATION_SEQUENCE`：先做电机 R/L，再做编码器偏置/方向。
- `AXIS_STATE_CLOSED_LOOP_CONTROL`：需要 `encoder_offset_valid == true`，否则报 `AXIS_ERROR_INVALID_STATE`。
- `AXIS_STATE_ERROR`：任一保护错误触发后进入，PWM 立即关断。

校准由主循环执行（`calibration_process()`），TMR2/ADC 中断继续运行并更新电流和编码器采样；校准期间喂 IWDG。

典型启动流程：

```text
1) 上电，电流零偏校准
2) 写 axis0.requested_state = 3    # FULL_CALIBRATION_SEQUENCE
   或 4 = MOTOR_CALIBRATION / 7 = ENCODER_OFFSET_CALIBRATION
3) 写 axis0.requested_state = 8    # 闭环控制
4) 写 axis0.controller.control_mode / input_mode
5) 写 axis0.controller.input_pos / input_vel / input_torque
6) 可选：写 axis0.save_configuration() 保存参数
```

## 5. 控制器模式

| 模式 | 值 | 行为 |
| --- | --- | --- |
| `CONTROL_MODE_VOLTAGE_CONTROL` | 0 | 预留 |
| `CONTROL_MODE_TORQUE_CONTROL` | 1 | `input_torque` 作为 q 轴电流给定 |
| `CONTROL_MODE_VELOCITY_CONTROL` | 2 | 速度误差 -> q 轴电流给定 |
| `CONTROL_MODE_POSITION_CONTROL` | 3 | 位置误差 -> 速度给定 -> q 轴电流给定 |

输入模式：

- `INPUT_MODE_INACTIVE`：保持上一次 setpoint。
- `INPUT_MODE_PASSTHROUGH`：直通。
- `INPUT_MODE_VEL_RAMP`：速度斜坡。
- `INPUT_MODE_POS_FILTER`：二阶位置跟踪滤波。
- `INPUT_MODE_TRAP_TRAJ`：梯形轨迹规划。
- `INPUT_MODE_TORQUE_RAMP`：力矩斜坡。

> 当前固件中 `input_torque` 的单位近似为 A（q 轴电流），没有除以力矩常数 `Kt`。如果要用 N·m，需要在 `controller.c` 中引入 `torque_constant`。

## 6. 编码器

- MT6816 通过 SPI3 读取，14 位绝对角度，16384 CPR；SPI 传输在 `encoder.c`。
- `encoder_pll.c`（无硬件依赖）用 PLL 计算连续机械位置、速度估计和电角度，并做相位插值，
  由 `tests/test_encoder_pll.c` 在主机侧验证。
- `encoder_pll_init()` 初始化全部估计量字段，`encoder_init()` 在此基础上再初始化 SPI，
  避免同一份初值写在两处。
- 极对数、CPR、方向、PLL 带宽均可通过 `odrive_config_t` 配置。
- 编码器方向与偏置由 `AXIS_STATE_ENCODER_OFFSET_CALIBRATION` 校准并写入 `encoder.pos_offset`。

## 7. 保护与安全

- PWM 默认关闭，只有进入 `CLOSED_LOOP_CONTROL` 且 `armed` 后才使能。
- 电流环未 armed 时直接返回，PWM 由状态机保持零矢量/关断。
- 过流保护比较峰值相电流；过压/欠压/过温保护在电流环回调中检查。
- 过温输入是板载 NTC（PB1/TEMP_2），10ms 采样（ADC1 规则组，不占 24kHz 注入组）；
  读数超出合理窗口视为传感器失效，`armed` 期间连续 10 次失效置
  `AXIS_ERROR_TEMPERATURE_SENSOR_FAILED` 并关 PWM。PB0/TEMP_1 只做监视，不参与保护。
- 编码器采样在 `armed`（闭环运行）期间连续 `ENCODER_FAULT_STREAK_LIMIT` 次失败
  （磁警告、SPI 读失败或 PLL 不可用）置 `AXIS_ERROR_ENCODER_FAILED`，立即 `pwm_disable()`
  并要求显式清错；未 armed 时不闭锁，以免编码器缺失/损坏时连电机 R/L 校准都无法进行。
- 电流矢量限幅、DC bus 功率限制、电压圆限制。
- IWDG 硬件看门狗固定 200ms；通信超时看门狗由 `axis.config.enable_watchdog` / `watchdog_timeout` 配置。
- 错误置位后 `axis_set_error()` 记录 `last_error` / `last_error_time_ms`，立即 `pwm_disable()`，状态机要求显式清除错误。
- 参数写入路径统一为：`param.c` 参数表 -> 写入 `g_odrive_config` -> `nvm_config_apply()`
  下发到 `g_axis`（唯一的下发函数，可重复调用且不会清除未保存的运行时校准结果）。

## 8. UART ASCII 协议（与 ODrive 对齐）

参数表在 `Src/param.c` 的 `s_params[]`：一行 = 规范路径 + 兼容旧路径 + 类型 + 精度 +
读指针 + 配置字段指针 + 可选副作用函数 + 上下界 + 越界处理方式。ASCII 与 CAN 两条协议
都通过 `param_write()` / `param_write_value()` 写入，因此同一个参数只有一份范围检查和
一份"写入配置 + 下发到 g_axis"的逻辑。

### 8.1 响应语义（照 ODrive 抄）

响应结尾用 `\r\n`，与 ODrive 一致。**这不是可选风格**：现成上位机是按这个格式解析的
（例如 ODriveArduino 的 `readFloat()` = `readString().toFloat()`）。

| 请求 | 响应 | 说明 |
| --- | --- | --- |
| `r vbus_voltage` | `24.087744` | **只回数值**，不带 `path: ` 前缀 |
| `r no.such.path` | `invalid property` | 未注册路径 |
| `r axis0.clear_errors` | `not implemented` | 存在但不可读 |
| `w axis0.controller.input_pos 1.5` | 无任何输出 | **成功静默** |
| `w no.such.path 1` | `invalid property` | |
| `w axis0.controller.input_pos` | `invalid command format` | 缺参数 |
| `w axis0.clear_errors 1` | `not implemented` | 只写属性的写路径关闭 |
| `w axis0.motor.config.pole_pairs 0` | `invalid value` | 越界被拒（ODrive 无此情形，自定文案） |
| `ss` | 无任何输出 | 保存成功静默，失败才回 `save failed` |
| 未知命令字母 | `unknown command` | |

写入持久化参数后立即调用 `nvm_config_apply()`，因此改了增益/限幅后无需重启，
也不需要先 `ss`；掉电保存才需要显式保存。改变电角度映射的参数
（`pole_pairs`、`cpr`）会显式作废编码器偏置校准，`CLOSED_LOOP_CONTROL` 被重新阻塞。

### 8.2 命令集

| 命令 | 语义 | 对应参数 |
| --- | --- | --- |
| `r <path>` | 读参数 | 见参数表 |
| `w <path> <value>` | 写参数 | 见参数表 |
| `p <m> <pos> [vel_ff] [torque_ff]` | 位置模式 + 设定点 | `control_mode`/`input_pos`/`input_vel`/`input_torque` |
| `v <m> <vel> [torque_ff]` | 速度模式 | `control_mode`/`input_vel`/`input_torque` |
| `c <m> <current>` | 力矩模式（单位 A） | `control_mode`/`input_torque` |
| `t <m> <destination>` | 梯形轨迹移动 | `input_mode=TRAP_TRAJ` + `control_mode` + `input_pos` |
| `f <m>` | 反馈，回 `pos vel` | `pos_estimate` / `vel_estimate` |
| `u <m>` | 喂通信看门狗 | 只校验参数合法性 |
| `ss` / `sr` / `sc` | 保存 / 重启 / 清错误 | `se`（擦除）未实现，回 `not implemented` |
| `s...` 其它 | `unknown command` | |

单轴固件：`<m>` 只接受 `0`，其它值回 `invalid motor <n>`。

> 与 ODrive 一致的细节：`p`/`v`/`c` 只改 `control_mode`，不动 `input_mode`；
> `t` 才把 `input_mode` 置为 `TRAP_TRAJ`。因此从 `t` 切回 `p` 时需要显式
> `w axis0.controller.input_mode 1`（ODrive 同样如此）。
> 命令末尾的 `; 注释` 与 `*校验和` 不解析（`strtof` 会在非数字处停止，不会报错）。

## 9. CAN Simple（与 ODrive 对齐）

- 帧格式：`can_id = node_id << 5 | cmd`，标准 11 位，多字节小端。
- 命令 ID `0x00`–`0x1D` 与 ODrive 的 `odrive-cansimple.dbc` 同名同号。
- 心跳 `0x01` 字段布局与 DBC 一致：
  `0..3 Axis_Error`、`4 Axis_State`、`5.0 Motor_Error_Flag`、`6.0 Encoder_Error_Flag`、
  `7.0 Controller_Error_Flag`、`7.7 Trajectory_Done_Flag`。三个标志位由
  `AXIS_ERROR_MASK_MOTOR/ENCODER/CONTROLLER`（`Inc/axis.h`）分类，不再用"任意错误"代替。
- 周期报文：心跳 100ms（`axis0.config.can.heartbeat_rate_ms` 可配）、编码器估计 10ms、
  母线电压电流 100ms。
- `GET_*` 命令既接受远程帧（RTR）也接受数据帧。AT32 库把 RTR 位放在 `frame_type`
  而不是 `id` 里（`at32f435_437_can.c:635`），所以 ID 保持干净、节点过滤不受影响。
  **待上板验证。**
- 波特率 `can.config.baud_rate`（默认 250 kbps，与 ODrive 出厂值一致）。
  位时序：`baudrate = fpclk / (baudrate_div * (3 + bts1 + bts2))`，取 BS1=13TQ、BS2=2TQ
  使采样点落在 87.5%，和恒为 16，标准波特率都能整除。**只在外设初始化时生效，即重启后生效**
  （与 ODrive "配置→保存→重启"的流程一致）。
- 互操作验证工具：`tools/odrive_can_smoke.py`（python-can + 官方 DBC）。

## 10. 后续扩展点

- USB CDC / ODrive 原生 USB 协议。
- CAN Simple / ASCII 协议继续补全。
- Flash 配置双备份/磨损均衡。
- 力矩常数辨识、增益调度、抗齿槽、无感 FOC、弱磁、MTPA。
- 制动电阻/泄放控制（本板无硬件，需外部改造）。
- 多轴支持（当前单轴）。
- 示波器/调试数据流（UART/CAN）。
