# 原理图引脚映射

来源：嘉立创 EDA 工程 `Mini_ODrive_AT32F435` / 板 `MT6816_3x3mos`，通过 EasyEDA Pro API 读取原理图网表整理。

## MCU `U36 AT32F435CGT7` (LQFP-48)

| Pin | 名称 | 网络 | 固件用途 |
| --- | --- | --- | --- |
| 1 | VBAT | VCC | 备份电源，接 3.3V |
| 2 | PC13 | CAN_120 | CAN 120Ω 终端电阻开关 |
| 3 | PC14 | - | 未连接 |
| 4 | PC15 | - | 未连接 |
| 5 | PH0 | X2 OUT / R90 | 8MHz 有源晶振输入（HEXT bypass） |
| 6 | PH1 | - | 未连接 |
| 7 | NRST | NRST | 复位，10k 上拉 + 100nF |
| 8 | VSSA/VREF- | GND | 模拟地 |
| 9 | VDDA/VREF+ | AVCC | 3.3V 模拟电源 |
| 10 | PA0 | CUR_A | ADC1_IN0 / 低边电流 A |
| 11 | PA1 | CUR_B | ADC1_IN1 / 低边电流 B |
| 12 | PA2 | CUR_C | ADC1_IN2 / 低边电流 C |
| 13 | PA3 | V_BUS | ADC1_IN3 / 母线电压分压 |
| 14 | PA4 | SPI1_CS | 外置 SPI 编码器 CS |
| 15 | PA5 | SPI1_SCK | 外置 SPI 编码器 SCK |
| 16 | PA6 | SPI1_MISO | 外置 SPI 编码器 MISO |
| 17 | PA7 | SPI1_MOSI | 外置 SPI 编码器 MOSI |
| 18 | PB0 | TEMP_1 | ADC1_IN8 / 外部温度输入 |
| 19 | PB1 | TEMP_2 | ADC1_IN9 / 板载 NTC |
| 20 | PB2 | RGB | WS2812B 数据，TMR20_CH1（`GPIO_MUX_2`）+ DMA1_CH1 |
| 21 | PB10 | USART_TX | USART3_TX，115200 |
| 22 | PB11 | USART_RX | USART3_RX，115200 |
| 23 | PH3 | IIC_SDA | I2C/软 I2C 数据，预留 |
| 24 | VDD | VCC | 3.3V |
| 25 | PB12 | - | 未连接 |
| 26 | PB13 | PWMA_L | TMR1_CH1N |
| 27 | PB14 | PWMB_L | TMR1_CH2N |
| 28 | PB15 | PWMC_L | TMR1_CH3N |
| 29 | PA8 | PWMA_H | TMR1_CH1 |
| 30 | PA9 | PWMB_H | TMR1_CH2 |
| 31 | PA10 | PWMC_H | TMR1_CH3 |
| 32 | PA11 | USB_DM | USB FS DM（OTGFS1，`GPIO_MUX_10`） |
| 33 | PA12 | USB_DP | USB FS DP（OTGFS1，`GPIO_MUX_10`） |
| 34 | PA13 | SWDIO | SWD 数据 |
| 35 | PH2 | IIC_SCL | I2C/软 I2C 时钟，预留 |
| 36 | VDD | VCC | 3.3V |
| 37 | PA14 | SWCLK | SWD 时钟 |
| 38 | PA15 | SPI3_CS | MT6816 片选（软件 CS） |
| 39 | PB3 | SPI3_SCK | MT6816 SPI 时钟 |
| 40 | PB4 | SPI3_MISO | MT6816 SPI 数据输入 |
| 41 | PB5 | SPI3_MOSI | MT6816 SPI 数据输出 |
| 42 | PB6 | AUX_H | 辅助 IO |
| 43 | PB7 | AUX_L | 辅助 IO |
| 44 | BOOT0 | BOOT0 | 10k 下拉 + 按键上拉 |
| 45 | PB8 | CAN_RX | CAN1_RX |
| 46 | PB9 | CAN_TX | CAN1_TX |
| 47 | VSS | GND | 地 |
| 48 | VDD | VCC | 3.3V |

## 功率级 `U20 FD6288Q` 与 MOSFET

| FD6288Q 引脚 | 网络 | 对应 MCU 引脚 | 说明 |
| --- | --- | --- | --- |
| 1 LIN1 | PWMC_L | PB15 | C 相低边输入 |
| 2 LIN2 | PWMB_L | PB14 | B 相低边输入 |
| 3 LIN3 | PWMA_L | PB13 | A 相低边输入 |
| 9 LO3 | M0_GL_A | - | A 相低边栅极 |
| 10 LO2 | M0_GL_B | - | B 相低边栅极 |
| 11 LO1 | M0_GL_C | - | C 相低边栅极 |
| 13 HO3 | M0_GH_A | - | A 相高边栅极 |
| 16 HO2 | M0_GH_B | - | B 相高边栅极 |
| 19 HO1 | M0_GH_C | - | C 相高边栅极 |
| 22 HIN1 | PWMC_H | PA10 | C 相高边输入 |
| 23 HIN2 | PWMB_H | PA9 | B 相高边输入 |
| 24 HIN3 | PWMA_H | PA8 | A 相高边输入 |

MOSFET 半桥：

- 高边：Q13(A)、Q14(B)、Q15(C)，漏极 DCBUS，源极 M0_SH_A/B/C。
- 低边：Q16(A)、Q17(B)、Q18(C)，漏极 M0_SH_A/B/C，源极 M0_SP_A/B/C。
- 分流：U50/U51/U52 = 2mΩ，接在 M0_SP_x 与 M0_SN_x（PGND）之间。

## 电流采样 `U16 RS724` 与 ADC

- U16.1/U16.2：`OUTA`/`-INA` 为 `REF`，缓冲 `+INA` 的 1.65V 参考。
- 每相一个差分放大通道：
  - `Vout = REF + (100k / 2k) * (V+ - V-)`
  - `V+` 经 2kΩ 接 M0_SN_x，并 100kΩ 到 REF。
  - `V-` 经 2kΩ 接 M0_SP_x，并 100kΩ 反馈到 OUT。
  - 因此 `Vout = REF - 50 * I * 0.002Ω`，即 `0.1 V/A` 反向。
- MCU 端：
  - `PA0` / ADC1_IN0 -> `CUR_A`（对应 U16 OUTD）
  - `PA1` / ADC1_IN1 -> `CUR_B`（对应 U16 OUTC）
  - `PA2` / ADC1_IN2 -> `CUR_C`（对应 U16 OUTB）
- 典型最大可测电流：`±16.5A`（以 REF=1.65V、3.3V 满量程计算）。

## 母线电压 `V_BUS`

- `DCBUS -- R1 10k -- V_BUS -- R8 1k -- GND`
- `V_BUS` 并 `C9 1uF`
- 分压比 `1/11`，ADC 满量程约 `36.3V`。

## 编码器 MT6816

| MT6816 引脚 | 网络 | MCU 引脚 |
| --- | --- | --- |
| 1 CSN | SPI3_CS | PA15 |
| 2 HVPP | VCC | 3.3V |
| 3 OUT | - | 未连接 |
| 4 VDD | VCC | 3.3V |
| 5 A/U | SPI3_MOSI | PB5 |
| 6 B/V | SPI3_MISO | PB4 |
| 7 Z/W | SPI3_SCK | PB3 |
| 8 GND | GND | 地 |

MT6816 使用 SPI mode 3（CPOL=1, CPHA=1），14 位角度，16384 CPR。
固件读取命令：先发 `0x83`，再读 2 字节；响应 16 位中 bit0 为奇偶校验，bit1 为磁警告，bit15..2 为 14 位角度。

## CAN 与终端电阻

- `U6 SIT3051TK`：`D` -> `CAN_TX` (PB9)，`R` -> `CAN_RX` (PB8)，`CANH/CANL` 经 `CN7` 和 `D9 NUP2105` 输出。
- `U17 GS4157B-CR`：由 `PC13` 控制，`R9 120Ω` 接 `COM`，`NC` 接 `CAN_H`，用于软件切换 120Ω 终端电阻。
- 固件默认 `PC13` 输出高电平（按 GS4157B 真值表，COM->NO 断开，终端关闭；`PC13=0` 时终端导通）。上板时用万用表确认。

## 电源

- USB-C `USBC1` VBUS -> `VUSB`，经 `D8 BAT54WS` 到 `5V`。
- `U7 XC6210B332MR`：5V -> 3.3V `VCC`。
- `U13 LP5907MFX-2.5` / 原理图标注为 3.3V 版本：5V -> `AVCC`（模拟 3.3V）。
- `U19 LGS5148` + `L3 22uH`：DCBUS -> `5V` 降压（给 CAN、USB、编码器、驱动等）。
- `VGS` 与 `5V` 通过 `short_symbol` 网络短接；FD6288Q 的 VCC 由 5V 供电。
- 本板没有制动斩波器。

## 温度输入

- `TEMP_1` (PB0)：`R15 3.3k` 下拉 + `C7 2.2uF`，测试点 `TP5`；可接外部 NTC 或电压输出温度传感器。
- `TEMP_2` (PB1)：板载 `RT1 10k NTC` 接 VCC，`R13 3.3k` 下拉 + `C46` 滤波；用于 MOS/板温。
