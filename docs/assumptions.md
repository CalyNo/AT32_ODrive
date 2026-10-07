# 假设与待上板验证项

本文件记录当前实现中无法仅凭原理图/数据手册完全确认、需要上板验证的内容。

## 1. SPI3 复用号

- 原理图引脚：PB3=SPI3_SCK，PB4=SPI3_MISO，PB5=SPI3_MOSI，PA15=SPI3_CS。
- AT32F435 数据手册确认 PB3/PB4/PB5 均支持 SPI3 功能。
- 固件暂定 SPI3 使用 `GPIO_MUX_6`（与 SDK 例程中 SPI3 的复用号一致）。
- 待验证：用示波器确认 SCK/MOSI 有输出，或参考 AT32F435 数据手册 IOMUX 表确认 PB3/4/5 的 SPI3 IOMUX 编号。

## 2. MT6816 SPI 协议

- 固件按以下协议读取：
  - SPI mode 3（CPOL=1, CPHA=1）。
  - 先发 `0x83`，再读 2 字节。
  - 响应 `u16`：bit0 奇偶校验，bit1 磁警告，bits15..2 为 14 位角度。
- 该协议来自公开 MT6816 Rust 驱动实现，与数据手册命令/寄存器描述一致。
- 待验证：读取原始值是否随磁铁旋转线性变化；若不对，尝试 16 位帧 `0x8300` 或去掉奇偶位检查。

## 3. 电流采样符号

- 固件定义“从逆变器流向电机”为正电流，此时 RS724 输出低于 REF。
- 电流换算：`I = (REF - V_adc) / (50 * 0.002)`。
- 待验证：用手转电机或小电流开环，确认正转矩指令对应正转速；若相反，将 `CURRENT_SENSE_GAIN` 符号或相序取反。

## 4. 相序

- 固件按 A->PA8/PB13，B->PA9/PB14，C->PA10/PB15 输出。
- FD6288Q 的 HIN/LIN 通道映射已按原理图核对。
- 待验证：开环旋转时方向、相序是否正确；可通过交换任意两相软件映射或设置 `ENCODER_DIRECTION_DEFAULT = -1` 调整。

## 5. CAN 终端电阻极性

- 原理图 U17 GS4157B-CR 由 PC13 控制，R9 120Ω 在 COM 与 CAN_L 之间，NC 接 CAN_H。
- 固件假设 `PC13 = 0` 时 COM->NC 导通，终端电阻接入；`PC13 = 1` 时断开。
- 待验证：用万用表量 CAN_H-CAN_L 电阻，确认 120Ω 开/关状态与 PC13 电平关系。

## 6. PWM 极性/死区

- FD6288Q 输入极性按高有效处理。
- 若上电时 MOSFET 直通或无法关断，立即断电并检查 `TMR_OUTPUT_ACTIVE_HIGH/LOW` 与 idle 状态。
- 死区时间暂定 20 个死区时钟，需按 FD6288Q 和 MOSFET 开关速度调整。

## 7. 晶振/时钟

- X2 为 8MHz 有源晶振，固件按 HEXT bypass 配置。
- 若实际 X2 是无源晶体，必须改回 `crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE)` 并配置负载电容，不能使用 bypass。
- 待验证：启动后通过 `system_core_clock` 或 UART 打印确认 288MHz。

## 8. 温度采样与过温保护

- 通道：`PB0 = TEMP_1 = ADC1_IN8`（外部/离板传感器），`PB1 = TEMP_2 = ADC1_IN9`（板载 NTC）。
  TEMP_2 由 `axis0.motor.fet_thermistor.*` 暴露并**参与过温跳闸**；TEMP_1 只做监视
  （可能未接传感器，因此不参与保护）。
- 采样方式：ADC1 **规则组**（不是 24kHz 注入组），由 1ms 任务按 10ms 节拍软件触发，
  每次只转换一个通道。注入组优先级更高，因此温度转换绝不会推迟电流采样。
- 换算：10k NTC 接 VCC + 3.3k 下拉，`board_temp_raw_to_celsius()`（Beta = 3950K）。
  合理窗口 raw 属于 [33, 3862]，对应 -40..+150°C；85°C 跳闸点对应 raw 约 3081。
- 传感器失效处理：读数落在窗口外（开路/短路/未焊）说明保护已失效；在 `armed` 期间
  连续 10 次（100ms）失效即置 `AXIS_ERROR_TEMPERATURE_SENSOR_FAILED` 并关 PWM。
  未 armed 时不闭锁，避免板上没焊 NTC 时连台架校准都做不了。失效期间保留上一次合理值，
  不把 -273°C 之类的伪值写进 `temp_mos`。
- 仍需上板确认：
  1. 实际 NTC `RT1 CMFA103J35000HANT` 的 R25 与 B 值，需按规格书核对 `TEMP_NTC_*`。
  2. 用 `r axis0.motor.fet_thermistor.raw` 对照室温与参考温度计验证换算。
  3. TEMP_1 的实际接法：接 NTC 才能套用上面的公式；若接电压输出型传感器需另写换算。

## 9. UART 浮点打印（GCC 构建）—— 已解决

- 原现象：`uart_comm_printf()` 的浮点格式在 GCC 构建下不输出数值。原因是 `firmware/Makefile`
  用了 `--specs=nano.specs` 但没有 `-u _printf_float`，newlib-nano 不会链接浮点格式化代码
  （证据：`_vfprintf_r` 与 `_vfiprintf_r` 同地址，且不存在 `_dtoa_r` / `_printf_float`）。
  而 Keil 构建（`useUlib=0`，ARMCC 标准库）能正常输出，两条构建路径行为不一致。
- 处理：`firmware/Makefile` 链接参数加 `-u _printf_float`。
  text 段 50020 -> 55120 字节（+5.1KB，占 1016KB 的 5%），可接受。
- 验证：`arm-none-eabi-nm build/AT32_ODrive.elf` 现在同时出现 `_dtoa_r` 与
  `_printf_float`，且 `_vfprintf_r` 内部确实 `bl _printf_float`。
- 这一步是 UART 上位机通路的前提：ASCII 协议读参数时只回数值，浮点必须真的能打印。

## 10. CAN 互操作（待上板验证）

- 命令 ID 与官方 `odrive-cansimple.dbc` 同名同号（0x00-0x1D），心跳字段布局已按 DBC 对齐。
- 波特率默认 250 kbps（与 ODrive 出厂值一致），由 `can.config.baud_rate` 配置，
  **重启后生效**（与 ODrive "改配置 -> 保存 -> 重启"的流程一致）。
- 待验证项：
  1. RTR 远程帧能否被正确应答。AT32 库把 RTR 标志放在 `frame_type` 而非 `id`
     （`at32f435_437_can.c:635`），固件按 `standard_id` 入队，因此 ID 应当保持干净。
  2. 心跳三个标志位（Motor/Encoder/Controller_Error_Flag）在真实故障下的表现。
  3. 250 kbps 位时序：BS1=13TQ、BS2=2TQ、采样点 87.5%，分频由
     `div = fpclk / (baudrate * 16)` 计算，标准波特率都能整除。
- 联调工具（均未上板运行过，属于 bring-up 辅助）：
  - `tools/odrive_can_smoke.py`：python-can + cantools + 官方 DBC，解码心跳/编码器估计/母线电压电流。
  - `tools/odrive_ascii_smoke.py`：pyserial，校验 ASCII 的响应语义（裸数值、写成功静默等）。

## 11. 保护阈值

- 默认母线过压 28V、欠压 7V、过温 85°C、电流限制 12A。
- 首次上电建议把电流限制改到 1-2A，并串联限流电源。
- `axis0.motor.config.current_lim` 通过 UART/CAN 写入时**没有上限检查**（只有下界 0），
  可以设到超过 `MOTOR_CURRENT_LIMIT_A`(12A) 的值；而 `MOTOR_CURRENT_LIMIT_MARGIN_A`
  是相对 12A 计算的，因此过流保护余量会被放大。上板前建议决定是否给该参数加上限。
