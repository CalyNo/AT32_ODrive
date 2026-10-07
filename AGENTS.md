# AT32_ODrive 项目约束

## 项目定位
- 硬件：嘉立创 EDA 工程 `Mini_ODrive_AT32F435`（板名 `MT6816_3x3mos`），主控 `AT32F435CGT7`。
- 固件：面向 AT32F435CGT7 的 ODrive 风格 FOC 驱动器，参考 ODrive 与 Artery AT32F435_437 Motor Control Library。
- 目标：单轴、低压 8-24V、3 路低边 2mΩ 采样、MT6816 SPI 磁编码器、CAN/UART 控制。
- 默认系统时钟 288MHz，PWM 24kHz，电流环 24kHz，速度/位置环 8kHz。

## 事实来源优先级
1. 嘉立创 EDA 工程中的原理图/网表（当前工程 `Mini_ODrive_AT32F435`）。
2. Artery 官方 `AT32F435_437_Firmware_Library` 与 `AT32F435_437_MC_Library_Project`。
3. ODrive 开源工程（行为/协议参考，不直接拷贝 GPL 代码进入本工程）。
4. 任何猜测都必须写入 `docs/assumptions.md`，不能静默写成事实。

## 硬件映射硬性约束
- MCU：`U36 AT32F435CGT7`，LQFP-48。
- 时钟：`X2` 为 8MHz 有源晶振，接 `PH0`；必须配置 HEXT bypass，不能按无源晶振配置。
- PWM：`TMR1` 互补输出。
  - `PA8` = PWM_A_H（`TMR1_CH1`）
  - `PA9` = PWM_B_H（`TMR1_CH2`）
  - `PA10` = PWM_C_H（`TMR1_CH3`）
  - `PB13` = PWM_A_L（`TMR1_CH1N`）
  - `PB14` = PWM_B_L（`TMR1_CH2N`）
  - `PB15` = PWM_C_L（`TMR1_CH3N`）
- 电流采样：3 路低边 2mΩ 分流 + `RS724` 差分放大，增益 50，偏置 `AVCC/2`。
  - `PA0` = CUR_A（ADC1_IN0）
  - `PA1` = CUR_B（ADC1_IN1）
  - `PA2` = CUR_C（ADC1_IN2）
  - `PA3` = V_BUS（ADC1_IN3），母线分压比 1/11。
- 板载编码器：`MT6816CT-STD` 使用 SPI3。
  - `PA15` = SPI3_CS
  - `PB3` = SPI3_SCK
  - `PB4` = SPI3_MISO
  - `PB5` = SPI3_MOSI
- 外部 SPI 编码器接口：SPI1。
  - `PA4` = SPI1_CS，`PA5` = SPI1_SCK，`PA6` = SPI1_MISO，`PA7` = SPI1_MOSI
- CAN：`PB8` = CAN1_RX，`PB9` = CAN1_TX，外部 `SIT3051TK`，PC13 控制 120Ω 终端电阻。
- UART：`PB10` = USART3_TX，`PB11` = USART3_RX，115200bps 默认。
- USB：`PA11` = USB_DM，`PA12` = USB_DP，USB FS device。
- 其它：`PB0` = TEMP_1，`PB1` = TEMP_2（NTC 10k 上拉/3.3k 下拉），`PB2` = WS2812B RGB。
- `BOOT0` 有 10k 下拉和按键上拉到 VCC；`NRST` 有 10k 上拉和 100nF。

## 安全约束（红线）
- 未完成电流零偏校准前不得使能 PWM。
- 未完成编码器方向/电角度校准前不得进入 `CLOSED_LOOP_CONTROL`。
- PWM 使能前必须先把三相占空比置为 50%（零矢量）并等待死区/驱动稳定。
- 过流、过压、欠压、过温、编码器错误任一触发立即 PWM 关断。
- 本板没有制动斩波器；禁止启用制动电阻功能；回馈能量必须通过电流限制或用户外部泄放处理。
- 修改任何引脚/外设映射前，必须同步更新 `docs/pinout.md` 与 `firmware/Inc/board.h`，二者不能漂移。

## 工作方式
- 新功能先在 `firmware/Inc` 定义接口，再在 `firmware/Src` 实现。
- 每次改动后必须至少通过：
  1. `cd firmware && mingw32-make -j` 交叉编译通过（0 warning）；
  2. `cd tests && mingw32-make run` 主机侧算法测试全部通过。
- 新增/删除 `firmware/Src` 下的源文件时，必须同时更新 `tools/gen_keil_project.py`
  的 `USER_SOURCES` 并重新生成 Keil 工程（`python tools/gen_keil_project.py`），
  否则 Makefile 与 Keil 工程会漂移。
- 不使用动态内存；不在中断中使用浮点长运算（除 FOC 必要的有限运算外）。
- 生成代码中的中文只用于注释，标识符/变量/文件名使用英文。
