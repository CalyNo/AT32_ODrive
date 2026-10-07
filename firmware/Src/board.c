#include "board.h"
#include "config.h"

#include <stddef.h>
#include <math.h>

extern unsigned int system_core_clock;

void board_clock_config(void)
{
  crm_reset();

  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
  pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);
  flash_clock_divider_set(FLASH_CLOCK_DIV_3);

  /* X2 is a 4-pin active oscillator, so PH0 must use HEXT bypass mode. */
  crm_hext_bypass(TRUE);
  crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
  while (crm_hext_stable_wait() == ERROR)
  {
  }

  /* 8 MHz * 144 / (1 * 4) = 288 MHz. */
  crm_pll_config(CRM_PLL_SOURCE_HEXT, 144, 1, CRM_PLL_FR_4);
  crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
  while (crm_flag_get(CRM_PLL_STABLE_FLAG) != SET)
  {
  }

  crm_ahb_div_set(CRM_AHB_DIV_1);
  crm_apb2_div_set(CRM_APB2_DIV_2);
  crm_apb1_div_set(CRM_APB1_DIV_2);

  crm_auto_step_mode_enable(TRUE);
  crm_sysclk_switch(CRM_SCLK_PLL);
  while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL)
  {
  }
  crm_auto_step_mode_enable(FALSE);

  system_core_clock_update();
}

static void board_gpio_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOH_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio_init_struct);

  /* Analog inputs: PA0..PA3, PB0, PB1. */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2 | GPIO_PINS_3;
  gpio_init(GPIOA, &gpio_init_struct);

  gpio_init_struct.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1;
  gpio_init(GPIOB, &gpio_init_struct);

  /* PWM pins: PA8/9/10 and PB13/14/15 use TMR1 alternate function. */
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_DOWN;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_pins = GPIO_PINS_8 | GPIO_PINS_9 | GPIO_PINS_10;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE8, GPIO_MUX_1);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_1);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE10, GPIO_MUX_1);

  gpio_init_struct.gpio_pins = GPIO_PINS_13 | GPIO_PINS_14 | GPIO_PINS_15;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE13, GPIO_MUX_1);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE14, GPIO_MUX_1);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE15, GPIO_MUX_1);

  /* CAN1: PB8 = RX, PB9 = TX. */
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_pins = GPIO_PINS_8 | GPIO_PINS_9;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE8, GPIO_MUX_9);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE9, GPIO_MUX_9);

  /* USART3: PB10 = TX, PB11 = RX. */
  gpio_init_struct.gpio_pins = GPIO_PINS_10 | GPIO_PINS_11;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE10, GPIO_MUX_7);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE11, GPIO_MUX_7);

  /* MT6816 SPI3: PB3 = SCK, PB4 = MISO, PB5 = MOSI, PA15 = software CS. */
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_pins = GPIO_PINS_3 | GPIO_PINS_4 | GPIO_PINS_5;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE3, GPIO_MUX_6);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE4, GPIO_MUX_6);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE5, GPIO_MUX_6);

  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_pins = GPIO_PINS_15;
  gpio_init(GPIOA, &gpio_init_struct);
  board_encoder_cs_set(false);

  /* External SPI1 encoder: PA4..PA7. */
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_pins = GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_6 | GPIO_PINS_7;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE4, GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE5, GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE6, GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE7, GPIO_MUX_5);

  /* CAN termination switch (GS4157B IN). Default: termination disabled. */
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_pins = GPIO_PINS_13;
  gpio_init(GPIOC, &gpio_init_struct);
  board_can_termination_set(false);

  /* Auxiliary and status pins. */
  gpio_init_struct.gpio_pins = GPIO_PINS_2;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_bits_reset(GPIOB, GPIO_PINS_2);

  gpio_init_struct.gpio_pins = GPIO_PINS_6 | GPIO_PINS_7;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_bits_reset(GPIOB, GPIO_PINS_6 | GPIO_PINS_7);

  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init_struct.gpio_pins = GPIO_PINS_2 | GPIO_PINS_3;
  gpio_init(GPIOH, &gpio_init_struct);
}

void board_init(void)
{
  board_gpio_init();
}

void board_watchdog_init(uint32_t timeout_ms)
{
  uint32_t reload;

  /*
   * Independent watchdog uses LSI (~40 kHz). With divider 64 the tick is
   * about 625 Hz, so reload = timeout_ms * 625 / 1000.
   */
  reload = (timeout_ms * 625u) / 1000u;
  if (reload < 1u)
  {
    reload = 1u;
  }
  if (reload > 4095u)
  {
    reload = 4095u;
  }

  wdt_register_write_enable(TRUE);
  wdt_reload_value_set((uint16_t)reload);
  wdt_divider_set(WDT_CLK_DIV_64);
  wdt_counter_reload();
  wdt_enable();
  wdt_register_write_enable(FALSE);
}

void board_watchdog_feed(void)
{
  wdt_counter_reload();
}

void board_can_termination_set(bool enable)
{
  /*
   * GS4157B-CR: COM->NC when IN is low, COM->NO when IN is high.
   * The 120 ohm resistor is on the NC side, so active-low enables it.
   */
  if (enable)
  {
    gpio_bits_reset(GPIOC, GPIO_PINS_13);
  }
  else
  {
    gpio_bits_set(GPIOC, GPIO_PINS_13);
  }
}

void board_encoder_cs_set(bool active)
{
  if (active)
  {
    gpio_bits_reset(GPIOA, GPIO_PINS_15);
  }
  else
  {
    gpio_bits_set(GPIOA, GPIO_PINS_15);
  }
}

/* Bounded blocking full-duplex byte transfer shared by SPI1 and SPI3. */
static uint8_t board_spi_transfer8(spi_type *spi, uint8_t tx)
{
  uint32_t retry = 0;

  while (spi_i2s_flag_get(spi, SPI_I2S_TDBE_FLAG) == RESET)
  {
    if (++retry > BOARD_SPI_TRANSFER_RETRY_LIMIT)
    {
      return 0u;
    }
  }

  spi_i2s_data_transmit(spi, tx);
  retry = 0;

  while (spi_i2s_flag_get(spi, SPI_I2S_RDBF_FLAG) == RESET)
  {
    if (++retry > BOARD_SPI_TRANSFER_RETRY_LIMIT)
    {
      return 0u;
    }
  }

  return (uint8_t)spi_i2s_data_receive(spi);
}

uint8_t board_spi3_transfer8(uint8_t tx)
{
  return board_spi_transfer8(SPI3, tx);
}

uint8_t board_spi1_transfer8(uint8_t tx)
{
  return board_spi_transfer8(SPI1, tx);
}

float board_vbus_raw_to_volts(uint16_t raw)
{
  return ((float)raw / VBUS_ADC_MAX) * VBUS_ADC_VREF_VOLT * VBUS_DIVIDER_RATIO;
}

float board_temp_raw_to_celsius(uint16_t raw)
{
  /*
   * TEMP_2 is an on-board 10k NTC to VCC with a 3.3k resistor to GND.
   * ADC measures the voltage across the 3.3k resistor:
   *   raw / 4095 = R_series / (R_ntc + R_series)
   * => R_ntc = R_series * (4095 / raw - 1)
   */
  float r_ntc;
  float inv_t;
  float inv_t0;
  const float kelvin = 273.15f;

  if (raw == 0u)
  {
    return -273.0f;
  }

  r_ntc = TEMP_NTC_SERIES_OHM * ((4095.0f / (float)raw) - 1.0f);
  if (r_ntc <= 0.0f)
  {
    return -273.0f;
  }

  inv_t0 = 1.0f / (TEMP_NTC_NOMINAL_C + kelvin);
  inv_t = inv_t0 + (1.0f / TEMP_NTC_BETA) * logf(r_ntc / TEMP_NTC_NOMINAL_OHM);
  return (1.0f / inv_t) - kelvin;
}

void board_uart_write(const uint8_t *data, uint32_t len)
{
  if (data == NULL)
  {
    return;
  }

  for (uint32_t i = 0; i < len; i++)
  {
    while (usart_flag_get(USART3, USART_TDBE_FLAG) == RESET)
    {
    }
    usart_data_transmit(USART3, data[i]);
  }

  while (usart_flag_get(USART3, USART_TDC_FLAG) == RESET)
  {
  }
}
