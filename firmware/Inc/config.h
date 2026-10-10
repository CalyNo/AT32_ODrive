#ifndef AT32_ODRIVE_CONFIG_H
#define AT32_ODRIVE_CONFIG_H

/*
 * Mini_ODrive_AT32F435 hardware/firmware configuration.
 * All values derived from the EasyEDA project "Mini_ODrive_AT32F435"
 * (board MT6816_3x3mos) and Artery AT32F435_437 MC library defaults.
 */

/* System clock: X2 is an 8 MHz active oscillator on PH0. */
#define BOARD_HEXT_HZ                   (8000000u)
#define BOARD_SYSCLK_HZ                 (288000000u)
#define BOARD_APB1_HZ                   (144000000u)
#define BOARD_APB2_HZ                   (144000000u)

/* Power stage */
#define BOARD_PWM_FREQ_HZ               (24000u)
#define BOARD_CURRENT_LOOP_FREQ_HZ      (24000u)
#define BOARD_VELOCITY_LOOP_FREQ_HZ     (8000u)
#define BOARD_PWM_DEADTIME_TICKS        (20u)
/* Poll bound for the blocking SPI byte transfers (MT6816 / external encoder).
 * Keeps a stuck bus from hanging the velocity-loop context. */
#define BOARD_SPI_TRANSFER_RETRY_LIMIT  (100000u)
/* Bounded UART TX poll: a mis-configured USART (e.g. TEN cleared, so TDBE never
 * sets) must not hang the whole firmware inside board_uart_write(). */
#define BOARD_UART_TX_RETRY_LIMIT       (100000u)

/* Low-side shunt + RS724 difference amplifier:
 * Vout = VREF - (Rf / Rin) * (Vshunt), with Rf = 100k, Rin = 2k.
 */
#define CURRENT_SENSE_SHUNT_OHM         (0.002f)
#define CURRENT_SENSE_GAIN              (50.0f)
#define CURRENT_SENSE_VREF_VOLT         (1.65f)
#define CURRENT_SENSE_ADC_VREF_VOLT     (3.3f)
#define CURRENT_SENSE_ADC_MAX           (4095.0f)

/* DC bus divider: R1=10k high side, R8=1k low side. */
#define VBUS_DIVIDER_RATIO              (11.0f)
#define VBUS_ADC_VREF_VOLT              (3.3f)
#define VBUS_ADC_MAX                    (4095.0f)

/* NTC and temperature inputs */
#define TEMP_NTC_SERIES_OHM             (3300.0f)  /* R13/R15 to GND */
#define TEMP_NTC_NOMINAL_OHM            (10000.0f)
#define TEMP_NTC_NOMINAL_C              (25.0f)
#define TEMP_NTC_BETA                   (3950.0f)
#define TEMP_ADC_VREF_VOLT              (3.3f)
/* Temperature sampling: PB0 = TEMP_1 (external), PB1 = TEMP_2 (on-board NTC).
 * Thermal time constants are seconds, so 100 Hz is far more than enough. */
#define TEMP_SAMPLE_PERIOD_MS           (10u)
/* Plausible reading window of the 10k NTC / 3.3k divider, from the Beta model:
 *   -40 C -> 401.9k ->  33 counts
 *  +150 C ->  199.6R -> 3862 counts
 * Outside this window the sensor is open, shorted or not fitted. */
#define TEMP_NTC_RAW_MIN_COUNTS         (33u)
#define TEMP_NTC_RAW_MAX_COUNTS         (3862u)
/* Consecutive out-of-window samples before the sensor fault is latched. */
#define TEMP_SENSOR_FAULT_STREAK_LIMIT  (10u)

/* Encoder */
#define ENCODER_MT6816_CPR              (16384.0f) /* 14-bit, 16384 counts/rev */
#define ENCODER_DIRECTION_DEFAULT       (1.0f)
/* Consecutive failed encoder samples before AXIS_ERROR_ENCODER_FAILED latches.
 * The velocity loop runs at BOARD_VELOCITY_LOOP_FREQ_HZ, so 3 samples ≈ 375 us. */
#define ENCODER_FAULT_STREAK_LIMIT      (3u)

/* Motor control defaults; tune per motor before closed-loop operation. */
#define MOTOR_POLE_PAIRS                (7u)
#define MOTOR_PHASE_RESISTANCE_OHM      (0.10f)
#define MOTOR_PHASE_INDUCTANCE_H        (0.00005f)
#define MOTOR_CURRENT_LIMIT_A           (12.0f)
#define MOTOR_CURRENT_LIMIT_MARGIN_A    (1.0f)
#define CONTROLLER_VEL_LIMIT_REV_S      (40.0f)
#define CONTROLLER_DC_BUS_POWER_LIMIT_W (240.0f)
#define CONTROLLER_DC_BUS_REGEN_LIMIT_W (80.0f)
#define PROTECTION_DC_BUS_CURRENT_MAX_A (30.0f)

/* Protection thresholds. */
#define PROTECTION_BUS_OVER_VOLTAGE_V   (28.0f)
#define PROTECTION_BUS_UNDER_VOLTAGE_V  (7.0f)
#define PROTECTION_TEMP_MAX_C           (85.0f)

/* Status LED: WS2812B-2020 data line on PB2 (see docs/pinout.md).
 * 25 % brightness is comfortable on a bench and still easy to read; the mode,
 * colour and brightness are runtime parameters (led.mode / led.color /
 * led.brightness). */
#define STATUS_LED_DEFAULT_BRIGHTNESS   (64u)
#define STATUS_LED_DEFAULT_COLOR        (0x00FFFFFFu)

/* Communication */
#define UART_BAUDRATE_DEFAULT           (115200u)
/* ODrive ships 250 kbit/s as its default CAN bit rate, so the same default is
 * used here to make the board drop-in compatible with existing CAN hosts.
 * Overridable at runtime through can.config.baud_rate (takes effect on reboot,
 * exactly like ODrive). */
#define CAN_BAUDRATE_DEFAULT            (250000u)
#define CAN_NODE_ID_DEFAULT             (0u)

/* Clock startup bounds (see board_clock_config()): the 8 MHz oscillator on PH0
 * can be absent/unpowered, and the boot must not hang silently waiting for it.
 * crm_hext_stable_wait() itself spins HEXT_STARTUP_TIMEOUT cycles per call. */
#define BOARD_HEXT_WAIT_ATTEMPTS        (32u)
#define BOARD_PLL_WAIT_LOOPS            (2000000u)

/* Bring-up instrumentation: print the reset cause and one line per init stage
 * on the UART.  Set to 0 to silence the boot path once the board is known
 * good; the "ready" banner is printed either way. */
#define BOOT_TRACE_ENABLE               (1u)

/* Firmware version */
#define FIRMWARE_VERSION_MAJOR          (0u)
#define FIRMWARE_VERSION_MINOR          (1u)
#define FIRMWARE_VERSION_REVISION       (0u)

#endif /* AT32_ODRIVE_CONFIG_H */
