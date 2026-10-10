#ifndef AT32_ODRIVE_BOARD_H
#define AT32_ODRIVE_BOARD_H

#include "at32f435_437.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Board: Mini_ODrive_AT32F435 / MT6816_3x3mos
 *
 * MCU: AT32F435CGT7 (LQFP-48, 1MB flash, 384KB SRAM)
 *
 * Pin map (from EasyEDA schematic):
 *   PA0  ADC1_IN0   CUR_A
 *   PA1  ADC1_IN1   CUR_B
 *   PA2  ADC1_IN2   CUR_C
 *   PA3  ADC1_IN3   V_BUS
 *   PA4  SPI1_CS    external encoder
 *   PA5  SPI1_SCK   external encoder
 *   PA6  SPI1_MISO  external encoder
 *   PA7  SPI1_MOSI  external encoder
 *   PA8  TMR1_CH1   PWMA_H
 *   PA9  TMR1_CH2   PWMB_H
 *   PA10 TMR1_CH3   PWMC_H
 *   PA11 USB_DM
 *   PA12 USB_DP
 *   PA13 SWDIO
 *   PA14 SWCLK
 *   PA15 SPI3_CS    MT6816
 *   PB0  ADC1_IN8   TEMP_1
 *   PB1  ADC1_IN9   TEMP_2 (on-board NTC)
 *   PB2  TMR20_CH1  WS2812B data (GPIO_MUX2)
 *   PB3  SPI3_SCK   MT6816
 *   PB4  SPI3_MISO  MT6816
 *   PB5  SPI3_MOSI  MT6816
 *   PB6  GPIO       AUX_H
 *   PB7  GPIO       AUX_L
 *   PB8  CAN1_RX
 *   PB9  CAN1_TX
 *   PB10 USART3_TX
 *   PB11 USART3_RX
 *   PB13 TMR1_CH1N  PWMA_L
 *   PB14 TMR1_CH2N  PWMB_L
 *   PB15 TMR1_CH3N  PWMC_L
 *   PC13 GPIO       CAN_120 termination switch
 *   PH2  GPIO       IIC_SCL (software or I2C2 if available)
 *   PH3  GPIO       IIC_SDA
 */

typedef enum
{
  BOARD_PHASE_A = 0,
  BOARD_PHASE_B = 1,
  BOARD_PHASE_C = 2
} board_phase_t;

void board_clock_config(void);

/*
 * False when the system clock is the intended HEXT (8 MHz active oscillator on
 * PH0) driven 288 MHz PLL.  When the oscillator does not come up, the clock
 * falls back to HICK so that the boot can still report the problem; the HICK is
 * only ~+-2-3% accurate, so the power stage must stay blocked (see axis_arm())
 * and CAN bit timing would be out of spec.
 */
bool board_clock_is_degraded(void);

/* Boot state mirrored into the parameter table for remote debugging:
 * board.reset_cause (CRM reset flags of this boot) and board.clock_degraded. */
extern uint32_t g_board_reset_cause;
extern uint32_t g_board_clock_degraded;

void board_init(void);
void board_watchdog_init(uint32_t timeout_ms);
void board_watchdog_feed(void);

/*
 * Reset cause, read from the CRM reset flags.  A boot loop caused by the IWDG
 * is only distinguishable from a firmware hang by looking at these flags, so
 * app_init() takes them before doing anything else.  Reading them clears them
 * (RSTFC), i.e. they describe the reset that led to the current boot.
 */
#define BOARD_RESET_CAUSE_NRST             (1u << 0)
#define BOARD_RESET_CAUSE_POR              (1u << 1)
#define BOARD_RESET_CAUSE_SOFTWARE         (1u << 2)
#define BOARD_RESET_CAUSE_WATCHDOG         (1u << 3)
#define BOARD_RESET_CAUSE_WINDOW_WATCHDOG  (1u << 4)
#define BOARD_RESET_CAUSE_LOW_POWER        (1u << 5)
uint32_t board_reset_cause_take(void);

void board_can_termination_set(bool enable);

void board_uart_write(const uint8_t *data, uint32_t len);

void board_encoder_cs_set(bool active);
uint8_t board_spi3_transfer8(uint8_t tx);
uint8_t board_spi1_transfer8(uint8_t tx);

float board_vbus_raw_to_volts(uint16_t raw);
float board_temp_raw_to_celsius(uint16_t raw);

#endif /* AT32_ODRIVE_BOARD_H */
