# AT32_ODrive

基于嘉立创 EDA 工程 `Mini_ODrive_AT32F435`（板名 `MT6816_3x3mos`）原理图实现的 AT32F435CGT7 ODrive 风格 FOC 固件项目。

> 当前固件为可编译、可运行的工程骨架 + 已实现的核心 FOC/电流环/编码器/通信代码。
> 首次上电必须完成电流零偏校准和编码器电角度校准，并在低母线电压、限流条件下逐步验证。
> 本板没有制动斩波器，禁止启用制动电阻功能。

## 硬件摘要

| 项目 | 参数 |
| --- | --- |
| MCU | AT32F435CGT7，LQFP-48，288MHz，1MB Flash，384KB SRAM |
| 时钟 | X2 8MHz 有源晶振接 PH0（HEXT bypass） |
| 功率级 | 3 路半桥，FD6288Q 栅极驱动，6× WSD4070DN MOSFET |
| 电流采样 | 3 路低边 2mΩ 分流 + RS724 差分放大，增益 50，偏置 AVCC/2 |
| 母线电压 | R1=10k / R8=1k 分压，ADC 满量程约 36.3V |
| 编码器 | 板载 MT6816CT-STD，SPI3（PA15/PB3/PB4/PB5，SPI mode 3） |
| 通信 | CAN 默认 250kbps（PB8/PB9，可由 `can.config.baud_rate` 配置）、UART 115200（PB10/PB11）、USB FS CDC 虚拟串口（PA11/PA12，OTGFS1） |
| 其它 | 温度 NTC（PB1 / TEMP_2）、温度输入（PB0 / TEMP_1）、WS2812B RGB（PB2） |
| 控制频率 | PWM 24kHz，电流环 24kHz，速度/位置环 8kHz |

完整的原理图引脚映射见 [`docs/pinout.md`](docs/pinout.md)，硬件与采样推导见 [`docs/hardware.md`](docs/hardware.md)，
**没接电机时的整板验证步骤见 [`docs/bringup.md`](docs/bringup.md)**（串口、母线/温度、电流零偏、编码器、状态灯、CAN、flash 配置、复位原因，以及 USB 现状）。

## 目录结构

```text
AT32_ODrive/
├── AGENTS.md                 项目约束与安全红线
├── README.md                 本文件
├── docs/
│   ├── pinout.md             原理图到 MCU 的引脚映射
│   ├── hardware.md           电源、电流采样、编码器、保护推导
│   ├── architecture.md       固件分层、任务调度与实时控制结构
│   ├── odrive_mapping.md     ODrive 风格接口与本项目对应关系
│   ├── odrive_feature_matrix.md  功能取舍与进度
│   └── bringup.md           无电机时的整板验证清单
├── firmware/
│   ├── Makefile              arm-none-eabi-gcc 构建
│   ├── Config/               AT32F435_437_conf.h 等配置
│   ├── Drivers/              Artery 固件库子集 + USB device 栈（CDC 虚拟串口）
│   ├── Inc/                  应用、系统、通信、控制、纯算法头文件
│   ├── Src/                  app/system/param/comm/control/BSP 源码
│   └── Startup/              启动文件与链接脚本
└── tests/                    x86 主机侧算法测试（foc/pid/traptraj/controller/encoder_pll/crc）
```

> 分层约定见 [`docs/architecture.md`](docs/architecture.md)：
> `foc / pid / traptraj / util / controller / encoder_pll / crc` 不依赖 CMSIS，
> 由 `tests/` 在主机侧直接编译测试；带硬件依赖的模块只通过交叉编译验证。

## 构建

两条构建路径都必须通过；主机侧测试用 `mingw32-make run` 一次跑完全部用例。

### 1. Keil MDK 工程

直接打开：

```text
firmware/AT32_ODrive.uvprojx
```

工程由 `tools/gen_keil_project.py` 从 `tools/keil_template.uvprojx` 生成。
**新增或删除 `firmware/Src` 下的源文件后必须重新生成**，否则 Makefile（通配 `Src/*.c`）
与 Keil 工程（显式文件列表）会漂移：

```bash
python tools/gen_keil_project.py
```

工程使用 `ArteryTek.AT32F435_437_DFP.2.2.8`。如果 Keil 提示找不到器件包，先安装：

```text
<你的用户目录>\AT32\ArteryTek.AT32F435_437_DFP.2.2.8.pack
```

或直接通过 Keil Pack Installer 在线安装 `ArteryTek::AT32F435_437_DFP` 2.2.8，无需本地 pack 文件。

命令行构建：

```bash
cd firmware
"D:\KEIL\UV4\UV4.exe" -b AT32_ODrive.uvprojx -j0 -o keil_build.log
```

已在本机验证通过：

```text
Build target 'AT32_ODrive'
".\build_keil\objects\AT32_ODrive.axf" - 0 Error(s), 0 Warning(s).
```

Keil 生成文件：

```text
firmware/build_keil/objects/AT32_ODrive.axf
firmware/build_keil/objects/AT32_ODrive.hex
```

### 2. GCC / Makefile 固件构建

需要 `arm-none-eabi-gcc`。本机已验证使用 Keil 自带的 GNU 工具链：

```bash
export PATH="/d/KEIL/ARM/12.2 rel1/bin:$PATH"   # Git Bash
cd firmware
mingw32-make -j
```

生成的固件文件：

```text
firmware/build/AT32_ODrive.elf
firmware/build/AT32_ODrive.hex
firmware/build/AT32_ODrive.bin
firmware/build/AT32_ODrive.map
```

本仓库已自带 AT32F435_437 固件库子集（`firmware/Drivers`），无需额外下载 SDK。
Artery 官方电机库参考包（`AT32F435_437_MC_Library_Project_V2.1.5`）需从 Artery 官网自行下载，其中包含官方 FOC、AS5047P/TLE5012B、Hall、无感等例程，仅作参考，不在本仓库内。

### 3. 主机侧算法测试

```bash
cd tests
mingw32-make run
```

覆盖：`foc`（Clarke/Park/SVPWM）、`pid`、`traptraj`、`controller`（控制模式/输入整形/限幅/电流环）、
`encoder_pll`（位置速度 PLL/环绕/电角度）、`crc`（配置镜像校验）。
也可以 `mingw32-make` 只构建不运行。

## 上位机 / 工具链互操作

| 上位机 | 走什么 | 能否共用 |
| --- | --- | --- |
| ODriveArduino、社区 ASCII 脚本 | UART ASCII | 可以（需 `--closed-loop` 等安全前提，见 `docs/assumptions.md`） |
| python-can + cantools + 官方 DBC | CAN Simple | 可以 |
| odrivetool / ODrive GUI | Fibre over USB | 最小 Fibre 0.1 端点已实现（USB vendor 接口 + endpoint 0 JSON + 少量读写），待上板验证 |

现成的联调脚本（**均未上板运行过**，属于 bring-up 辅助）：

```bash
# CAN: 解码心跳/编码器估计/母线电压电流，走官方 DBC
python tools/odrive_can_smoke.py --interface canalystii --channel 0 --bitrate 250000
# UART: 校验 ASCII 响应语义（裸数值、写成功静默）
python tools/odrive_ascii_smoke.py --port COM3
```

依赖：`pip install python-can cantools pyserial`。


## 当前已实现

- 288MHz 时钟初始化（8MHz HEXT bypass + PLL）。
- TMR1 三相互补 PWM（PA8/9/10 + PB13/14/15），中心对齐，24kHz，死区可配。
- ADC1 注入组：PA0/PA1/PA2 三相低边电流 + PA3 母线电压，由 TMR1_CH4 触发。
- 电流零偏校准。
- 电机相电阻 / 相电感阻塞式校准。
- 编码器电角度偏置与方向校准（开环电角度扫描 + 线性拟合）。
- 电流矢量限幅、简化 DC bus 功率限制与 DC bus 过流硬保护。
- 编码器故障闭锁：连续 `ENCODER_FAULT_STREAK_LIMIT` 次采样失败置错误并关断 PWM。
- R/L 前馈、bEMF 前馈、d/q 解耦与电压圆限制。
- 过流、过压、欠压保护；过温保护（PB1/TEMP_2 板载 NTC，10ms 采样，含 NTC 开路/短路检测）。
- MT6816 SPI3 磁编码器驱动；SPI1 外置编码器接口预留。
- Clarke / Park / 反 Park / SVPWM 和电流环 PI。
- 编码器 PLL 速度估计与相位插值（`encoder_pll.c`，有主机侧测试）。
- ODrive 风格运行时配置 `odrive_config_t` + 内部 Flash 参数保存/读取（CRC-32 校验、版本号、双份布局检查）。
- IWDG 硬件看门狗 + ODrive 风格通信超时看门狗。
- ODrive 风格轴状态机：`IDLE`、电机校准、编码器校准、全校准、`CLOSED_LOOP_CONTROL`、错误状态。
- 力矩/速度/位置三种控制模式，输入模式支持直通、速度斜坡、位置滤波、梯形轨迹、力矩斜坡。
- CAN Simple：命令编号与官方 `odrive-cansimple.dbc` 同名同号，心跳字段布局对齐 ODrive，支持心跳、错误、状态、模式、输入、限制、TrapTraj、Iq、母线电压电流等。
- USB vendor 接口 + Fibre 0.1 最小服务端：与 CDC 组成复合设备，暴露 `fw_version_*`、`hw_version_*`、`serial_number`、`vbus_voltage`、`axis0.error/current_state/requested_state` 以及少量编码器/控制器输入端点，供开源 ODrive GUI / odrivetool 发现和读写。当前端点为最小子集，未实现订阅与完整 0.5.x 对象树。
- UART ASCII：与 ODrive 响应语义一致（`r` 只回数值、`w` 成功静默、结尾 CRLF），命令集 `r`/`w`/`p`/`v`/`c`/`t`/`f`/`u`/`ss`/`sr`/`sc`，由 `param.c` 参数表统一驱动（详见 `docs/architecture.md` 第 8 节）；因此 **ODriveArduino 等现成 ASCII 上位机可直接使用**。
- WS2812B 状态灯（PB2 = TMR20_CH1 + DMA，硬件产生波形、不关中断）：错误红色闪烁码、校准蓝闪、闭环绿、空闲蓝；颜色/模式/亮度可用 `w led.mode|led.color|led.brightness` 或 CAN 改写（运行时属性，重启回默认）。
- 启动可观测性：`BOOT_TRACE_ENABLE` 打开时串口打印复位原因（POR/NRST/IWDG/软件）与各初始化阶段，IWDG 在 `app_init` 末尾才使能，启动卡点不会被复位循环掩盖。

## 尚未完成/上板前需要做的事

- MT6816 的 SPI 时序和磁警告位需要在实际板上验证；必要时调整 SPI 时钟/极性/命令。
- NTC 的 R25/B 值与换算需要用参考温度计上板核对（见 `docs/assumptions.md` 第 8 节）。
- 电流采样点、PWM 极性和死区需要接示波器确认；FD6288Q 输入极性若相反需调整 `TMR_OUTPUT_ACTIVE_*`。
- 电机参数（极对数、相电阻、相电感、电流环 PI、速度/位置增益）需要按实际电机整定。
- 校准流程已实现阻塞式 R/L 和编码器偏置/方向，但必须在低电流、限流电源下首次验证。
- 力矩常数辨识、增益调度、抗齿槽、无感 FOC、弱磁、MTPA 尚未实现。
- USB CDC（虚拟串口）已接入，与 UART 共用同一套 ASCII 协议；ODrive 原生 Fibre 0.1 最小协议已接入（USB vendor 接口、endpoint 0 JSON、fw/hw/vbus/axis0 基础端点），完整端点树、订阅和 DFU 未实现。
- 示波器/调试数据流、制动电阻、多轴支持未实现。
- 本板无制动斩波器，回馈能量只能通过限流或外部泄放处理。

## 参考

- EasyEDA 工程：`Mini_ODrive_AT32F435`（板 `MT6816_3x3mos`）。
- Artery 官方 SDK：`AT32F435_437_Firmware_Library_V2.2.6`。
- Artery 官方电机库：`AT32F435_437_MC_Library_Project_V2.1.5`。
- ODrive 开源固件参考（Gitee 镜像）：<https://gitee.com/xujun0621/ODrive>。
- MT6816 数据手册：MagnTek MT6816 AMR 磁编码器。
- MT6816 驱动协议参考：<https://docs.rs/mt6816/0.1.1/src/mt6816/lib.rs.html>。

## 许可

- 本项目**原创代码**（`firmware/Inc`、`firmware/Src`、`firmware/Config`、`firmware/Startup` 自研部分、`tests/`、`tools/`、`docs/`）以
  **GNU General Public License v3.0** 发布，全文见 [`LICENSE`](LICENSE)。
- `firmware/Drivers/` 下的 Artery AT32F435_437 固件库与 CMSIS 属于**第三方代码**，
  版权归 Artery Technology 与 Arm 所有，按其原始许可随本仓库分发，**不适用**本项目的 GPL-3.0。
- ODrive 代码仅作为行为与协议参考，未直接拷贝 GPL 代码进入本工程。
- 本工程涉及电机与功率级控制，按 GPL-3.0 的免责条款**不提供任何担保**；
  上电前请先阅读 [`AGENTS.md`](AGENTS.md) 中的安全红线。
