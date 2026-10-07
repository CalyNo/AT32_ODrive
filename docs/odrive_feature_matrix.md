# MiniOdrive 对 ODrive 功能取舍矩阵

本文件定义 Mini_ODrive_AT32F435 单轴固件相对 ODrive 的功能范围。

## 当前进度快照

已完成本轮新增：

- ODrive 风格运行时配置结构 `odrive_config_t`。
- 内部 Flash 参数保存/读取/擦除（CRC32、版本、默认回退）。
- 梯形轨迹规划 `TrapTraj`，并接入 `INPUT_MODE_TRAP_TRAJ`。
- 控制器输入模式扩展：Inactive、Passthrough、Vel Ramp、Pos Filter、TrapTraj、Torque Ramp。
- 编码器 PLL 速度估计、相位插值、编码器带宽配置。
- IWDG 看门狗，可由持久化配置启用。
- 电机相电阻/相电感校准。
- 编码器电角度偏置与方向校准（开环电角度扫描 + 线性拟合）。
- 电流矢量限幅、简化 DC bus 功率限制与 DC bus 过流硬保护。
- R/L 前馈、bEMF 前馈、d/q 解耦与电压圆限制。
- ODrive 风格通信超时看门狗与故障时间戳。
- CAN Simple 命令编号对齐 ODrive，补齐控制/限制/错误/状态/编码器/总线电压命令。
- UART ASCII 路径扩展到常用配置、校准、错误和反馈。
- Keil/GCC 双构建保持 0 error 0 warning，主机侧测试增加 `test_traptraj`。

仍需继续：

- 力矩常数辨识、增益调度、抗齿槽、无感 FOC、弱磁、MTPA。
- USB CDC。
- 故障日志深度、参数合法性检查。

## 决策图例

- **实现**：硬件支持，且对 MiniOdrive 核心应用有价值。
- **部分实现**：已有基础版本，需要继续补齐。
- **后续**：硬件支持但优先级靠后或工作量较大。
- **跳过（硬件）**：MiniOdrive 硬件没有对应器件/接口，明确不做。
- **跳过（单轴）**：ODrive 的双轴/镜像功能，MiniOdrive 单轴不做。

## 1. 电机与电流控制

| ODrive 功能 | MiniOdrive 决策 | 说明 |
| --- | --- | --- |
| 三相 PWM / SVPWM | 实现 | TMR1 互补 PWM，24kHz |
| 三低边分流电流采样 | 实现 | U50/U51/U52，2mΩ + RS724 增益 50 |
| Clarke / Park / 反 Park | 实现 | 已完成 |
| 电流 PI 控制 | 实现 | 已完成，需继续完善 |
| 电流限幅器 | 实现 | 计划加入 d/q 电流矢量限幅 |
| R*I + ωL*I 前馈 | 实现 | 用实测 R/L 参数 |
| bEMF 前馈 | 实现 | 使用编码器速度和磁链估计 |
| d/q 解耦 | 实现 | 与 R/L 前馈一起 |
| 电流环带宽自动换算增益 | 实现 | 由 R/L 和 bandwidth 计算 PI |
| 过调制 / 圆限制 | 实现 | 电压矢量限幅 |
| 相电阻 / 相电感校准 | 实现 | 上电校准流程 |
| 力矩常数 / 极对数校准 | 部分实现 | 极对数手动配置，力矩常数后续支持 |
| 电流环自整定 | 后续 | 非必须，可先用带宽换算 |
| 单分流 / 双分流 | 跳过（硬件） | MiniOdrive 固定三低边分流 |
| 无感 FOC / HFI | 后续 | 硬件可以做，但优先级低于有编码器闭环 |
| 弱磁 / MTPA | 后续 | 适合高速/凸极电机，非第一版必须 |
| ACIM 感应电机 | 后续 | ODrive 有，MiniOdrive 主要面向 PMSM/BLDC |
| 制动斩波器 / Brake Resistor | 跳过（硬件） | 板上无制动 MOSFET |
| DRV8301 配置与故障 | 跳过（硬件） | 使用 FD6288Q，无 DRV8301 |

## 2. 编码器

| ODrive 功能 | MiniOdrive 决策 | 说明 |
| --- | --- | --- |
| MT6816 SPI 绝对编码器 | 实现 | 板载编码器，SPI3 |
| AS5047P / MT6825 / MT6835 SPI | 实现 | 通过 SPI1 外置接口，寄存器协议不同 |
| 编码器 PLL 速度估计 | 实现 | 替代当前一阶差分 |
| 相位插值 | 实现 | 用速度在计数值之间插值 |
| 编码器偏置校准 | 实现 | 完整 ODrive 式扫描 |
| 编码器方向自动识别 | 实现 | Direction Find |
| Index Search | 跳过（硬件） | MT6816 无 Index 输出，SPI1 接口也未引出 Index |
| 增量 ABZ 编码器 | 跳过（硬件） | 未引出 A/B/Z |
| Hall 编码器 | 跳过（硬件） | 未引出 Hall |
| Sin/Cos 编码器 | 跳过（硬件） | 未引出 Sin/Cos |
| CUI / AEAT / RLS / MA732 | 后续 | 仅当外接 SPI 编码器需要时再加 |
| 双编码器 / 负载编码器 | 跳过（单轴） | 单轴不需要 |

## 3. 轴状态机与校准

| ODrive 功能 | MiniOdrive 决策 | 说明 |
| --- | --- | --- |
| IDLE | 实现 | 已完成 |
| STARTUP_SEQUENCE | 实现 | 按配置执行启动校准链 |
| FULL_CALIBRATION_SEQUENCE | 实现 | Motor + Encoder Offset |
| MOTOR_CALIBRATION | 实现 | R/L 测量 |
| ENCODER_OFFSET_CALIBRATION | 实现 | 当前为简化版，需完整化 |
| ENCODER_INDEX_SEARCH | 跳过（硬件） | 无 Index |
| ENCODER_DIR_FIND | 实现 | 无 Index 也可用全周扫描 |
| CLOSED_LOOP_CONTROL | 部分实现 | 已完成基础闭环，需完善限制与安全 |
| LOCKIN_SPIN | 实现 | 用于校准和调试 |
| HOMING | 跳过（硬件） | 无专用限位/回零输入，AUX 可扩展但不做 |
| HALL_POLARITY / PHASE | 跳过（硬件） | 无 Hall |
| ANTICOGGING | 后续 | 需要高分辨率编码器和标定流程 |
| 双轴 / Mirror | 跳过（单轴） | MiniOdrive 单轴 |
| 启动配置选择 | 实现 | 用 NVM 配置决定启动链 |

## 4. 控制器与轨迹

| ODrive 功能 | MiniOdrive 决策 | 说明 |
| --- | --- | --- |
| 力矩控制 | 实现 | 已完成 |
| 速度控制 | 实现 | 已完成 |
| 位置控制 | 实现 | 已完成 |
| Voltage / Gimbal 模式 | 后续 | 低优先级 |
| Passthrough | 实现 | 已完成 |
| Vel Ramp | 实现 | 已完成基础版 |
| Pos Filter | 实现 | 二阶位置跟踪滤波 |
| TrapTraj | 实现 | 梯形轨迹规划 |
| Torque Ramp | 实现 | 力矩斜坡 |
| Mirror | 跳过（单轴） | 单轴无镜像对象 |
| Tuning / Chirp | 后续 | 调试用 |
| 圆整 setpoint | 实现 | 位置环形范围 |
| 速度限制 / 容差 | 部分实现 | vel_limit 用于限幅；vel_limit_tolerance 只被读取，未触发超速错误 |
| 力矩模式速度限制 | 未实现 | 力矩模式直接给 i_q，不做速度限制 |
| 惯量前馈 | 未实现 | `controller.inertia` 有字段但未被控制律使用 |
| 增益调度 | 后续 | 非必须 |
| 抗齿槽 | 后续 | 标定工作量大 |
| 跟随误差 | 部分实现 | 只计算 pos_error/vel_error，未触发 AXIS_ERROR_POSITION_LIMIT_VIOLATION |

## 5. 通信

| ODrive 功能 | MiniOdrive 决策 | 说明 |
| --- | --- | --- |
| UART ASCII 协议 | 实现 | 响应语义与 ODrive 一致（裸数值/写成功静默/CRLF），命令 r/w/p/v/c/t/f/u/ss/sr/sc |
| CAN Simple | 实现 | 0x00-0x1D 与官方 DBC 同名同号；心跳字段布局对齐（含三个 error flag） |
| CAN 波特率 | 实现 | 默认 250kbps（ODrive 出厂值），`can.config.baud_rate` 可配，重启生效 |
| USB FS 硬件 | 实现 | PA11/PA12 已引出 |
| USB CDC | 未实现 | PA11/PA12 仅硬件引出，无 USB 代码；当前用 UART/CAN |
| ODrive Native USB / Fibre | 后续 | 工作量大，若必须兼容 odrivetool 再做 |
| I2C 接口 | 后续 | PH2/PH3 可用，但无板上 I2C 器件 |
| Step/Dir | 后续 | AUX 引脚可扩展，当前不做 |
| PWM 输入 | 跳过（硬件） | 无专用 RC PWM 输入 |
| GPIO/PWM 输出 | 后续 | AUX_H/AUX_L 可扩展 |
| 机械抱闸 | 跳过（硬件） | 无抱闸驱动 |
| Oscilloscope / Logger | 后续 | 可先用 UART/CAN 简易输出 |

## 6. 配置、NVM 与系统

| ODrive 功能 | MiniOdrive 决策 | 说明 |
| --- | --- | --- |
| 运行时参数树 | 实现 | ODrive 风格 config 结构 |
| save_configuration() | 实现 | 保存到 AT32 内部 Flash |
| erase_configuration() | 未实现 | 无处调用，原空实现 `nvm_config_erase()` 已删除；需要时按 save 的擦除流程补回 |
| 参数 CRC / 版本 | 实现 | 防止半写坏配置 |
| 默认配置 | 实现 | 编译期默认 + 恢复默认 |
| 看门狗 | 实现 | IWDG + 通信超时 |
| 通信超时保护 | 实现 | 可配置 watchdog_timeout |
| 错误时间戳 / 故障记录 | 实现 | 至少记录 last_error |
| Bootloader / DFU | 后续 | 产品化再做 |
| UART IAP / USB IAP | 后续 | 可通过 SDK 例程扩展 |
| RTOS | 跳过 | 单轴裸机足够，保持简单 |
| 栈监控 | 后续 | 可加简单高水位检查 |
| 多轴任务 | 跳过（单轴） | MiniOdrive 单轴 |

## 7. 安全保护

| ODrive 功能 | MiniOdrive 决策 | 说明 |
| --- | --- | --- |
| 过压 / 欠压 | 实现 | 已完成基础 |
| 过流 | 实现 | 已完成基础 |
| 过温 | 实现 | PB1/TEMP_2 板载 NTC，10ms 采样（ADC1 规则组）+ 传感器失效检测；PB0/TEMP_1 仅监视 |
| DC Bus 功率限制 | 实现 | 限制回馈/驱动功率 |
| I_bus 估计 | 实现 | 由三相电流和占空比估算 |
| 速度/位置跟随误差 | 未实现 | 无定位误差保护 |
| 编码器错误 | 实现 | SPI 读失败/磁警告/PLL 不可用连续 3 次 -> AXIS_ERROR_ENCODER_FAILED 并关 PWM |
| 门极驱动故障 | 跳过（硬件） | FD6288Q 无故障输出 |
| 制动电阻过载 | 跳过（硬件） | 无制动电阻 |
| 通信超时 | 实现 | 看门狗 |
| 参数非法检查 | 实现 | apply_config 时检查 |

## 8. 明确不做的硬件相关项

- 制动斩波器 / Brake Resistor。
- DRV8301 / DRV8305 相关配置和故障。
- 双轴、镜像轴、负载编码器。
- Index / ABZ / Hall / SinCos 编码器。
- 板上无抱闸、无风扇、无专用限位输入。
- ODrive 官方硬件中的板载 AS5047P/CUI 编码器。
