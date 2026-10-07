#include "encoder.h"

#include "board.h"
#include "config.h"
#include "encoder_pll.h"

#include <stddef.h>

/*
 * Hardware half of the encoder module: SPI3 (on-board MT6816) and SPI1 (external
 * magnetic encoder) transport.  All estimation math lives in encoder_pll.c so it
 * can be tested on the host.
 */

static void encoder_spi3_init(void)
{
  spi_init_type spi_init_struct;

  crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, TRUE);

  spi_default_para_init(&spi_init_struct);
  spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
  spi_init_struct.mclk_freq_division = SPI_MCLK_DIV_16;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
  /* MT6816 requires SPI mode 3: CPOL=1, CPHA=1. */
  spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_HIGH;
  spi_init_struct.clock_phase = SPI_CLOCK_PHASE_2EDGE;
  spi_init_struct.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI3, &spi_init_struct);
  spi_enable(SPI3, TRUE);
}

void encoder_spi1_init(void)
{
  spi_init_type spi_init_struct;

  crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);

  spi_default_para_init(&spi_init_struct);
  spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
  spi_init_struct.mclk_freq_division = SPI_MCLK_DIV_16;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
  spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi_init_struct.clock_phase = SPI_CLOCK_PHASE_2EDGE;
  spi_init_struct.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI1, &spi_init_struct);
  spi_enable(SPI1, TRUE);
}

uint16_t encoder_mt6816_read_raw(bool *magnet_ok)
{
  uint8_t hi;
  uint8_t lo;
  uint16_t frame;
  uint16_t angle;

  board_encoder_cs_set(true);
  (void)board_spi3_transfer8(0x83u); /* MT6816 read-angle command. */
  hi = board_spi3_transfer8(0x00u);
  lo = board_spi3_transfer8(0x00u);
  board_encoder_cs_set(false);

  frame = ((uint16_t)hi << 8) | (uint16_t)lo;
  angle = frame >> 2; /* bits [1:0] are magnet warning and parity. */

  if (magnet_ok != NULL)
  {
    *magnet_ok = ((frame & 0x0002u) == 0u);
  }

  return angle & 0x3FFFu;
}

static uint16_t encoder_spi1_read_raw(bool *magnet_ok)
{
  uint8_t hi;
  uint8_t lo;
  uint16_t frame;
  uint16_t angle;

  gpio_bits_reset(GPIOA, GPIO_PINS_4);
  (void)board_spi1_transfer8(0xFFu);
  hi = board_spi1_transfer8(0xFFu);
  lo = board_spi1_transfer8(0xFFu);
  gpio_bits_set(GPIOA, GPIO_PINS_4);

  frame = ((uint16_t)hi << 8) | (uint16_t)lo;
  if (magnet_ok != NULL)
  {
    *magnet_ok = ((frame & 0x4000u) == 0u);
  }
  angle = frame & 0x3FFFu;
  return angle;
}

void encoder_init(encoder_t *enc, encoder_type_t type, float cpr, float direction)
{
  if (enc == NULL)
  {
    return;
  }

  encoder_pll_init(enc, type, cpr, direction, (int32_t)MOTOR_POLE_PAIRS,
                   ENCODER_PLL_DEFAULT_BANDWIDTH_HZ);

  if (type == ENCODER_TYPE_MT6816)
  {
    encoder_spi3_init();
  }
  else
  {
    encoder_spi1_init();
  }
}

bool encoder_update(encoder_t *enc, float dt)
{
  uint16_t raw;
  bool magnet_ok = true;

  if ((enc == NULL) || (dt <= 0.0f))
  {
    return false;
  }

  if (enc->type == ENCODER_TYPE_MT6816)
  {
    raw = encoder_mt6816_read_raw(&magnet_ok);
  }
  else
  {
    raw = encoder_spi1_read_raw(&magnet_ok);
  }

  if (!magnet_ok)
  {
    enc->magnet_ok = false;
    enc->error_count++;
    return false;
  }
  enc->magnet_ok = true;

  return encoder_pll_update(enc, raw, dt);
}
