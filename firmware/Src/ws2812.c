#include "ws2812.h"
#include "system_time.h"
#include "board.h"

#include <stdbool.h>
#include <stdint.h>

#define WS2812_GPIO_PORT  GPIOB
#define WS2812_GPIO_PIN   GPIO_PINS_2

static uint8_t s_red;
static uint8_t s_green;
static uint8_t s_blue;

static inline void ws2812_high(void)
{
  gpio_bits_set(WS2812_GPIO_PORT, WS2812_GPIO_PIN);
}

static inline void ws2812_low(void)
{
  gpio_bits_reset(WS2812_GPIO_PORT, WS2812_GPIO_PIN);
}

static void ws2812_send_byte(uint8_t value)
{
  for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1)
  {
    if ((value & mask) != 0u)
    {
      ws2812_high();
      system_delay_cycles(202u); /* ~0.70 us @288MHz */
      ws2812_low();
      system_delay_cycles(173u); /* ~0.60 us */
    }
    else
    {
      ws2812_high();
      system_delay_cycles(101u); /* ~0.35 us */
      ws2812_low();
      system_delay_cycles(230u); /* ~0.80 us */
    }
  }
}

void ws2812_init(void)
{
  ws2812_low();
  s_red = 0u;
  s_green = 0u;
  s_blue = 0u;
}

void ws2812_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
  s_red = r;
  s_green = g;
  s_blue = b;
}

void ws2812_update(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();

  ws2812_send_byte(s_green);
  ws2812_send_byte(s_red);
  ws2812_send_byte(s_blue);

  ws2812_low();
  system_delay_us(60u);

  if (primask == 0u)
  {
    __enable_irq();
  }
}
