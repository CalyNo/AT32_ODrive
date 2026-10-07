# ODrive 风格接口映射

本项目参考 ODrive 的层次与接口命名，但为了在 AT32F435 + 官方 AT32F435_437 固件库上运行，做了简化实现。

## 1. 对象映射

| ODrive 概念 | 本项目 | 说明 |
| --- | --- | --- |
| `Axis` | `axis_t` / `g_axis` | 单轴状态机、保护、输入输出 |
| `Controller` | `controller_t` | 力矩/速度/位置控制、TrapTraj、电流 PI |
| `Motor` / `CurrentControl` | `axis_current_loop_callback()` | 三相电流采样、Clarke/Park、SVPWM、前馈与解耦 |
| `Encoder` | `encoder_t` | MT6816 SPI3 / SPI1 外置编码器，PLL |
| `config` | `odrive_config_t` / `nvm_config.c` | 运行时配置与 Flash 持久化 |
| `requested_state` | `axis.requested_state` | ODrive 状态编号兼容 |
| `axis.error` | `axis.error` | 位掩码错误，含 `last_error` 时间戳 |

## 2. 状态编号

与 ODrive 常用状态编号一致：

| 状态 | 值 | 状态 |
| --- | --- | --- |
| `AXIS_STATE_IDLE` | 1 | 已实现 |
| `AXIS_STATE_STARTUP_SEQUENCE` | 2 | 未实现 |
| `AXIS_STATE_FULL_CALIBRATION_SEQUENCE` | 3 | 已实现 |
| `AXIS_STATE_MOTOR_CALIBRATION` | 4 | 已实现 |
| `AXIS_STATE_ENCODER_OFFSET_CALIBRATION` | 7 | 已实现 |
| `AXIS_STATE_CLOSED_LOOP_CONTROL` | 8 | 已实现 |
| `AXIS_STATE_LOCKIN_SPIN` | 9 | 未实现 |
| `AXIS_STATE_ENCODER_DIR_FIND` | 10 | 未实现（当前由校准流程拟合方向） |
| `AXIS_STATE_HOMING` | 11 | 跳过（硬件无专用限位） |

## 3. CAN Simple

标准 11 位 ID：`(node_id << 5) | cmd`，默认 `node_id = 0`。
命令编号已对齐 ODrive `CANSimple`。

### 接收命令

| cmd | 名称 | 数据 | 本固件 |
| --- | --- | --- | --- |
| 0x00 | NMT | - | 忽略 |
| 0x01 | Heartbeat | error(u32), state(u32) | 接收忽略，周期发送 |
| 0x02 | Estop | - | 置 `AXIS_ERROR_ESTOP_REQUESTED` |
| 0x03 | Get Motor Error | - | 返回 `axis.error` |
| 0x04 | Get Encoder Error | - | 返回 `encoder.error_count` |
| 0x05 | Get Sensorless Error | - | 返回 0 |
| 0x06 | Set Axis Node ID | node_id(u32) | 更新运行时 node id |
| 0x07 | Set Axis Requested State | state(u32) | 已实现 |
| 0x08 | Set Axis Startup Config | - | 忽略 |
| 0x09 | Get Encoder Estimates | - | 周期/请求发送 pos,vel |
| 0x0A | Get Encoder Count | - | 返回 shadow_count, raw |
| 0x0B | Set Controller Modes | control_mode(u32), input_mode(u32) | 已实现 |
| 0x0C | Set Input Pos | pos(float), vel(int16 × 0.001), torque(int16 × 0.001) | 已实现 |
| 0x0D | Set Input Vel | vel(float), torque(float) | 已实现 |
| 0x0E | Set Input Torque | torque(float) | 已实现 |
| 0x0F | Set Limits | vel_limit(float), current_lim(float) | 已实现 |
| 0x10 | Start Anticogging | - | 未实现 |
| 0x11 | Set Traj Vel Limit | vel_limit(float) | 已实现 |
| 0x12 | Set Traj Accel Limits | accel(float), decel(float) | 已实现 |
| 0x13 | Set Traj Inertia | inertia(float) | 已实现 |
| 0x14 | Get Iq | - | 返回 Iq setpoint / measured |
| 0x15 | Get Sensorless Estimates | - | 返回 0,0 |
| 0x16 | Reset ODrive | - | `NVIC_SystemReset()` |
| 0x17 | Get Bus Voltage/Current | - | 返回 vbus, i_bus |
| 0x18 | Clear Errors | - | 清除错误并回到 IDLE |
| 0x19 | Set Linear Count | count(int32) | 已实现 |
| 0x1A | Set Pos Gain | pos_gain(float) | 已实现 |
| 0x1B | Set Vel Gains | vel_gain(float), vel_integrator_gain(float) | 已实现 |
| 0x1C | Get ADC Voltage | gpio(u8) | gpio 0=vbus，1..3=相电流 |
| 0x1D | Get Controller Error | - | 返回 0 |

### 周期发送

- Heartbeat：`g_odrive_config.comm.can_heartbeat_rate_ms`，默认 100ms。
- Encoder estimates：固定 10ms。
- Bus voltage/current：固定 100ms。
- 可通过 `0x09`、`0x14`、`0x17` 请求立即发送。

## 4. UART ASCII 子集

支持常用 ODrive 风格路径：

响应语义与 ODrive 一致（**这是上位机共用的前提**）：读取只回裸数值、写入成功不回任何内容、
结尾 CRLF、失败回 `invalid property` / `invalid command format` / `not implemented` /
`invalid value`。详细表格见 `docs/architecture.md` 第 8 节。

```text
r axis0.error
r axis0.last_error
r axis0.last_error_time_ms
r axis0.current_state
r axis0.requested_state
r axis0.encoder.pos_estimate
r axis0.encoder.vel_estimate
r axis0.encoder.shadow_count
r axis0.encoder.raw
r axis0.motor.phase_resistance
r axis0.motor.phase_inductance
r axis0.motor.current_control.Id_setpoint
r axis0.motor.current_control.Iq_setpoint
r axis0.controller.input_pos
r axis0.controller.input_vel
r axis0.controller.input_torque
r axis0.controller.pos_setpoint
r axis0.controller.vel_setpoint
r axis0.motor.config.calibration_current
r axis0.motor.config.current_lim
r axis0.motor.config.pole_pairs
r axis0.encoder.config.cpr
r axis0.controller.config.pos_gain
r axis0.controller.config.vel_gain
r axis0.motor.fet_thermistor.temperature
r axis0.motor.fet_thermistor.raw
r axis0.motor.motor_thermistor.temperature
r axis0.motor.motor_thermistor.raw
r vbus_voltage
r fw_version_major
r fw_version_minor
r axis0.controller.control_mode
r axis0.controller.input_mode
r can.config.baud_rate
r axis0.config.can.heartbeat_rate_ms

w axis0.requested_state <uint>
w axis0.error 0
w axis0.clear_errors 1
w axis0.motor.config.calibration_current <float>
w axis0.motor.config.current_lim <float>
w axis0.motor.config.pole_pairs <int>
w axis0.motor.config.torque_constant <float>
w axis0.motor.config.r_wl_ff_enable <0|1>
w axis0.motor.config.bemf_ff_enable <0|1>
w axis0.encoder.config.cpr <float>
w axis0.encoder.config.bandwidth <float>
w axis0.controller.config.pos_gain <float>
w axis0.controller.config.vel_gain <float>
w axis0.controller.config.vel_integrator_gain <float>
w axis0.controller.config.vel_limit <float>
w axis0.controller.config.vel_ramp_rate <float>
w axis0.controller.config.torque_ramp_rate <float>
w axis0.controller.config.input_filter_bandwidth <float>
w axis0.controller.config.traj_vel_limit <float>
w axis0.controller.config.traj_accel_limit <float>
w axis0.controller.config.traj_decel_limit <float>
w axis0.controller.control_mode <uint>
w axis0.controller.input_mode <uint>
w axis0.controller.input_pos <float>
w axis0.controller.input_vel <float>
w axis0.controller.input_torque <float>
w axis0.can.node_id <0..63>            # 别名 axis0.config.can.node_id
w can.config.baud_rate <10000..1000000>  # 重启后生效
w axis0.config.can.heartbeat_rate_ms <ms>
w axis0.controller.config.control_mode <0..3>
w axis0.controller.config.input_mode <0..8>
w axis0.config.save_configuration()
```

命令字母（与 ODrive 同名同义）：

```text
p <m> <pos> [vel_ff] [torque_ff]    位置模式 + 设定点
v <m> <vel> [torque_ff]             速度模式
c <m> <current>                     力矩模式（A）
t <m> <destination>                 梯形轨迹移动
f <m>                               反馈，回 "pos vel"
u <m>                               喂通信看门狗
ss / sr / sc                        保存 / 重启 / 清错误
```

`<m>` 只接受 `0`（单轴）。

通信参数：USART3，115200 8N1。

## 5. 与 ODrive 的主要差异

- 没有 ChibiOS/FreeRTOS，使用裸机中断 + 主循环。
- 没有 Fibre/USB 原生协议；当前使用 CAN Simple + UART ASCII。
  **odrivetool / ODrive GUI 走 Fibre over USB，无法直接连接本板**；
  能与本固件共用的上位机是 ODriveArduino 等 ASCII 客户端，以及
  python-can + 官方 DBC 的 CAN 客户端（见 `tools/odrive_can_smoke.py`）。
- 控制器量纲做了简化，`input_torque` 近似为 q 轴电流，没有除以力矩常数 `Kt`。
- 电机 R/L、编码器方向/偏置已实现；力矩常数辨识、增益调度、抗齿槽、无感 FOC、弱磁、MTPA 未实现。
- 没有制动电阻控制，因为本板硬件没有制动斩波器。
- 配置参数可通过 UART（`w ...`）或 CAN（Set 类命令）修改；写持久化参数会立即下发到运行时，
  但只有 `ss` / `w ...save_configuration` 才写入内部 Flash。启动时加载，
  magic/version/CRC 无效则回退默认值并重写。
- CAN 波特率默认 250kbps（与 ODrive 出厂值一致），`can.config.baud_rate` 修改后需重启生效。
