# 硬件与采样推导

## 1. 三相 PWM

- `TMR1` 中心对齐模式，计数器时钟 288MHz。
- PWM 频率 24kHz，ARR = `288e6 / (2 * 24000) = 6000`。
- 主通道：`PA8/PA9/PA10` -> `TMR1_CH1/2/3` -> `PWMA_H/PWMB_H/PWMC_H`。
- 互补通道：`PB13/PB14/PB15` -> `TMR1_CH1N/2N/3N` -> `PWMA_L/PWMB_L/PWMC_L`。
- 死区由 `TMR1` 死区发生器设置，约 20 个死区时钟；FD6288Q 自身也有死区/防直通。
- 零矢量：三相占空比 50%。
- 上电顺序：GPIO 默认下拉 -> `pwm_init()` 初始化并保持定时器关闭 -> 校准电流偏置 -> 状态机允许后再 `pwm_enable()`。

> 极性：FD6288Q 的 HIN/LIN 通常为高有效。若实测驱动输入为低有效，需要把 `pwm.c` 中主/互补通道 `oc_polarity` / `occ_polarity` 改为 `TMR_OUTPUT_ACTIVE_LOW`，并同步修改 idle 状态。

## 2. 低边电流采样

本板每相低边 MOSFET 源极与 PGND 之间串 2mΩ 分流，经 RS724 差分放大：

```text
Vout = VREF + (Rf / Rin) * (V+ - V-)
     = 1.65V + (100k / 2k) * (V(M0_SN_x) - V(M0_SP_x))
     = 1.65V - 50 * I_phase * 0.002Ω
     = 1.65V - 0.1 * I_phase
```

因此：

- 电流正方向定义为从逆变器流向电机。
- 正电流使 ADC 电压低于 1.65V。
- 电流换算：

```c
I = (1.65V - V_adc) / (50 * 0.002Ω)
```

ADC 12 位、3.3V 参考，则：

```text
I(A) ≈ (1.65 - raw * 3.3 / 4095) / 0.1
```

固件在 `adc.c` 中先测量零电流偏置，然后按上述公式换算。
采样时刻由 `TMR1_CH4` 触发 ADC 注入组，位置为 `ARR - 1 - 0.4us` 对应的比较值，与 Artery 官方电机库的 3-shunt 采样点一致。上板需用示波器确认至少两相低边在采样时刻导通。

## 3. 母线电压

```text
V_BUS = V_DCBUS * R8 / (R1 + R8) = V_DCBUS / 11
V_DCBUS = raw * 3.3 / 4095 * 11
```

满量程约 36.3V。固件保护阈值默认：

- 过压：28V
- 欠压：7V

## 4. 温度

- `TEMP_2`（PB1）为板载 NTC：
  - NTC 接 VCC，3.3k 下拉到 GND。
  - `R_ntc = 3300 * (4095 / raw - 1)`
  - B 值公式：`1/T = 1/T0 + (1/B) * ln(R_ntc / R0)`，其中 `R0 = 10k`，`T0 = 25°C`，`B ≈ 3950K`。
- `TEMP_1`（PB0）为外部输入，当前只做 ADC 采集接口，具体换算取决于外接传感器。

## 5. 保护策略

当前固件实现：

- 过压 / 欠压：比较 `axis.vbus_voltage` 与阈值。
- 过流：比较三相电流绝对值最大值与 `current_limit + margin`。
- 过温：比较 `axis.temp_mos` 与 85°C。
- 任一错误置位后进入 `AXIS_STATE_ERROR`，PWM 立即关闭，必须通过 `axis0.error=0` + `requested_state=IDLE` 清除。
- 本板没有制动斩波器，回馈能量必须通过电机侧限流或外部泄放处理；不要在高速大惯量下长时间反拖。

## 6. 电源与上电注意

- `VUSB -> D8 -> 5V -> U7 -> VCC`。
- `DCBUS -> U19 LGS5148 -> 5V`。
- USB 和 DCBUS 同时供电时，5V 与 VUSB 通过二极管隔离；上电前确认电源无短路。
- `LP5907` 必须使用 3.3V 版本（原理图值），否则 AVCC 不对，电流偏置 REF 将不是 1.65V。
- `R61 0R` 将 PGND 与 GND 单点连接；大电流回路不要通过信号地。
