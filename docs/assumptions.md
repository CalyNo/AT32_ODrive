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

- X2 为 8MHz 有源晶振，接 `PH0`（第 5 脚），固件按 HEXT bypass 配置：
  `crm_hext_bypass(TRUE)` → `crm_clock_source_enable(HEXT, TRUE)` → 等 HEXTSTBL →
  `crm_pll_config(HEXT, 144, 1, FR_4)` = 288MHz → 切 SCLK。
  顺序与 RM 要求一致（`HEXTBYPS` 只能在 HEXT 关闭时写）；`PH0/PH1` 不需要额外 IOMUX 配置，
  手册 Table 6-8（Port H）里没有 HEXT 选项，HEXT 由 `CRM_CTRL[16]=HEXTEN` 使能。
- 若实际 X2 是无源晶体，必须去掉 bypass 并配置负载电容，不能按有源晶振配置。
- **启动等待是有界的**（`BOARD_HEXT_WAIT_ATTEMPTS`/`BOARD_PLL_WAIT_LOOPS`）：有源晶振缺失、
  没供电、或 OE 未拉高时 `HEXTSTBL` 永远不置位，无界等待会让整块板"完全静默"
  （无串口、无灯、也不复位），这是最难定位的启动故障。现在会退到内部 HICK（也是 8MHz）
  走同一套 PLL 参数，串口能起来并在第一行报告：
  ```text
  boot: reset=... clock=DEGRADED(HICK)     ← HEXT 没起来
  boot: reset=... clock=HEXT+PLL 288MHz    ← 正常
  ```
- 降级运行时（HICK ±2-3%）**功率级被锁死**：`pwm_enable()` 直接返回、`axis_arm()` 置
  `AXIS_ERROR_INVALID_STATE`。因为 PWM 频率、死区、ADC 采样点、电流环周期都由系统时钟推导，
  3% 误差会让电流限值失效，所以宁可不让驱动。
- 硬件排查顺序（完全没有串口输出时）：
  1. 示波器测 `PH0`（`X2 OUT`，经 `R90`）：应有 8MHz、3.3V 方波；没有就查 X2 的 VDD/GND、
     OE 脚电平，以及 R90/焊接；
  2. 有方波但 `clock=DEGRADED(HICK)`：查 HEXT 使能/旁路位（本固件已配）；
  3. `clock=HEXT+PLL 288MHz` 却仍无输出：问题在串口侧（TX/RX 接反、共地、适配器电平）。
- 待验证：`clock=HEXT+PLL 288MHz` 且串口正常打印，即证明 HEXT + PLL 达到 288MHz。

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

## 12. 状态指示灯（WS2812B-2020 / PB2）

- 引脚与复用：PB2 网络 `RGB`。参考手册 Table 6-2 确认 `PB2 + MUX2 = TMR20_CH1`
  （`MUX1 = TMR2_CH4` 不可用，TMR2 已用于 8kHz 速度环），固件使用 `GPIO_MUX_2`。
- 时钟：TMR20 属 APB2 高级定时器；本板 APB2 = 144MHz，定时器时钟 = 288MHz。
- 位时序：PWM 周期 1.25µs（`ARR = 359`），`T0H = 350ns`（100 tick），`T1H = 700ns`
  （201 tick），因此 `T0L = 900ns`、`T1L = 550ns`，均落在 WS2812B 的
  T0H 0.25-0.55µs / T1H 0.65-0.95µs / T0L 0.65-0.95µs / T1L 0.20-0.50µs 窗口内。
- 帧格式：G-R-B、MSB first，DMA 帧 25 个 entry（24 个比特 + 1 个帧尾槽）。
  复位/锁存时间不单独计时，靠"帧与帧之间整段低电平"满足（状态灯任务 100ms 一帧）。
- 亮度默认 64/255（2020 封装亮度较低，必要时用 `led.brightness` 提高）。
- `led.mode` / `led.color` / `led.brightness` **只存在运行时**（`g_status_led_*`），重启回默认值。
  原因：持久化需要给 `odrive_config_t` 加字段并提升 `NVM_CONFIG_VERSION`，而版本号一变，
  flash 里已保存的电机/编码器校准会被当作旧版本丢弃并重写默认值 —— 为了一个指示灯设置
  作废已完成校准不划算。若以后确实要持久化，应加 v3→v4 迁移（读到 v3 时填充 LED 默认值
  再存 v4），而不是直接顶版本号。
- 待上板验证：
  1. 示波器测 PB2：位宽 1.25µs、高电平 350ns/700ns，并确认 DMA 管线没有错位
     （首个比特被重复或丢掉会让整帧颜色偏移）。建议用 `w led.mode 1` +
     `w led.color 841920`（0x804020 近似）与预期颜色对比。
  2. 确认 `DMAMUX_DMAREQ_ID_TMR20_CH1 = 0x56` 与实际请求线一致（若 LED 完全无反应，
     优先检查这里，以及 DMAMUX 的 `tblsel` 是否使能）。
  3. 25% 亮度下列错误闪烁码是否可数清；2020 封装偏暗时可调到 128。
- 已消除的旧行为：原位翻转驱动每帧关中断约 90µs，会推迟 24kHz 电流环并可能丢 UART
  字节；新实现完全不关中断。

## 13. 启动阶段的 IWDG（已修复）

- 现象：上电/复位后芯片反复回到 `app_init` 入口（复位循环），串口一个字符都出不来。
- 原因（结构性问题，不是 LED 驱动挂死）：`board_watchdog_init(200u)` 原来在 `app_init`
  **中段**就使能 IWDG，而 IWDG 的特点是
  1. 由 LICK 独立计数，**系统复位不会让它停止或清零**，只能靠上电（POR）复位，
  2. 一旦使能无法关闭。
  因此这段之后的任何一次卡住（`board_clock_config()` 里等 HEXT 稳定/PLL 锁定、
  `adc_init()` 等 RDY/自校准、`adc_calibrate_current_offsets()` 等转换完成、
  `board_uart_write()` 等 TX 标志）都会在 200ms 后被 IWDG 复位 → 再进 `app_init`
  → 再卡住 → 再复位，永远看不到真正的卡点。排查时若用调试器复位（不是断电），
  上一次运行遗留的 IWDG 计数器仍在跑，现象一样。
- 修复：
  1. `app_init` 开头先 `board_reset_cause_take()` 读并清 CRM 复位标志；
  2. 紧接着 `board_watchdog_feed()`，把上一次遗留的计数器重新装载成完整窗口
     （写 0xAAAA 只重装计数器，不会使能 WDT）；
  3. UART 提前到 `board_init()` 之后初始化，于是启动路径可以打点；
  4. `board_watchdog_init(200u)` 挪到 `app_init` **最后**，之后由 1ms 系统任务喂狗。
     启动阶段卡住时不再被复位掩盖，而是原地卡住（调试器直接停在卡点）。
- `BOOT_TRACE_ENABLE`（`Inc/config.h`，默认 1）打开时串口会逐条打印：
  `boot: reset=<POR|NRST|SW|IWDG|...> (0xNN)`、`boot: clock+gpio+adc ok`、
  `boot: config loaded|defaults`，最后才是 `AT32_ODrive x.y.z ready`。
  板子稳定后可置 0 关掉。
- 注意：若芯片的用户系统数据区（option bytes）打开了"硬件看门狗"，则 POR 后 WDT 已经
  在跑（默认 DIV/RLD，超时约 0.4s）；此时启动路径更长就要小心，第 2 步的 feed 能重新
  装载计数器，缓解但不消除这种风险。
- 排查建议：出现复位循环时先 **断电重上电**（POR 关掉 WDT），再看卡在哪一步；
  banner 里的 `reset=` 字段能直接告诉你是 IWDG 复位还是 POR/NRST/软件复位。

## 14. USART3 收发器未使能（已修复，串口从来没通过）

- 现象：上电完全没有任何串口字符；调试器里 PC 停在 `board_uart_write()` 的
  `while (usart_flag_get(USART3, USART_TDBE_FLAG) == RESET) {}` 里出不来。
- 原因：`uart_comm_init()` 只做了 `usart_init()` + `usart_enable()`，**漏了
  `usart_transmitter_enable(USART3, TRUE)`（CTRL1.TEN）和 `usart_receiver_enable()`（REN）**。
  官方 SDK 例程（`project/at_start_f435/examples/usart/interrupt/src/main.c`）的顺序是
  `usart_init` → `usart_transmitter_enable` → `usart_receiver_enable` → 中断 → `usart_enable`。
  - 少 `TEN`：TDBE 不会置位 → `board_uart_write()` 死等 → 启动 trace、banner、参数回显
    全部发不出来，整板表现为"完全静默"（和时钟没起振的现象一模一样，容易误判）；
  - 少 `REN`：主机发来的字节进不了接收寄存器，`USART3_IRQHandler` 也永不触发 →
    `r`/`w` 命令从来无法生效。
- 修复：补上两行（顺序同官方例程），并把 `board_uart_write()` 的两个等待改成**有界**
  （`BOARD_UART_TX_RETRY_LIMIT`），这样即使以后配错 USART 也不会再把整机挂死。
- 排错经验：**"完全没有串口输出"有两个完全不同的原因** ——
  1. `clock=DEGRADED(HICK)` 之类说明固件起来了、问题在外设配置（本节就是）；
  2. 一个字符都没有时，要先在调试器里确认 PC 停在哪个等待，再决定查时钟还是查串口配置。
  两者都已在固件里改成有界等待，不会再静默死循环。

## 15. USB FS CDC 虚拟串口（PA11/PA12）

- 目标：插上电脑出现一个 COM 口，复用现有 ASCII 协议，便于没有 USB-TTL 时调试。
- 实现：vendor 了 AT32 SDK 的 USB device 栈（`Drivers/USB_Device/`，含 `at32f435_437_usb.c`
  与 CDC class），胶水层 `Src/usb_cdc.c`；USB 中断入口 `OTGFS1_IRQHandler`，
  优先级 `IRQ_PRIORITY_USB = 2`（与 UART/CAN 同级，USB FS 需要及时响应）。
- 时钟：OTGFS 需要精确 48MHz，本板 `288MHz / 6 = 48MHz`（`crm_usb_clock_div_set(CRM_USB_DIV_6)`）。
  **`board.clock_degraded = 1` 时 USB 不启动**（HICK ±3% 无法枚举），与"降级锁功率级"一致。
- **VBUS 检测必须关闭**（`Config/usb_conf.h` 里 `USB_VBUS_IGNORE`）：AT32 的 OTG VBUS 引脚
  固定是 `PA9`，而本板 `PA9 = PWM_B_H`。代价是无法感知 VBUS 掉电/拔出瞬间状态，CDC 功能不受影响。
- 两个"例程钩子"必须自己提供，否则 **Keil 链接会报 L6218E**（`--gc-sections` 的 GCC 构建
  会把这些未被调用的代码裁掉从而掩盖问题）：
  - `usb_delay_ms()`：库的远程唤醒路径里 delay 10ms → 用 DWT 忙等实现（不依赖中断、
    时间有界，见 `Src/usb_cdc.c`）；
  - `usb_usart_config()`：CDC class 用它在 SET_LINE_CODING 时改桥接串口波特率 → 这里
    **空实现**，虚拟串口速率是名义值，USART3 保持自己的 115200。
  已加 `make check-link`（不带 `--gc-sections` 链接全部对象）来提前暴露这类问题。
- 传输方式：TX/RX 各一条环形缓冲，`usb_cdc_poll()` 在 1ms 通信任务里每次搬运一个 64 字节包；
  USB 库的 `usbd_ept_send()` 持有缓冲区指针，因此暂存缓冲在一次传输完成前不会被覆盖。
- 待上板验证：
  1. 能否枚举成 CDC（设备管理器出现串口）；`r usb.connected` 是否为 1；
  2. 双向收发：`r board.core_clock`、`w led.self_test 1`，并观察 `usb.rx_bytes` / `usb.tx_dropped`；
  3. 与 UART3 同时打开时，两边是否都能看到全部回复（广播）。
- 未实现：ODrive 原生 USB 协议（Fibre/DFU），`odrivetool` 仍不可用。
