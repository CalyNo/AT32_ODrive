#include "status_led.h"
#include "axis.h"
#include "ws2812.h"

#include <stdbool.h>

static uint8_t s_red;
static uint8_t s_green;
static uint8_t s_blue;
static bool s_dirty;

void status_led_init(void)
{
  ws2812_init();
  s_red = 0u;
  s_green = 0u;
  s_blue = 0u;
  s_dirty = true;
}

void status_led_task(void *context)
{
  uint8_t red = 0u;
  uint8_t green = 0u;
  uint8_t blue = 0u;

  (void)context;

  if (g_axis.error != AXIS_ERROR_NONE)
  {
    red = 32u;
  }
  else if (g_axis.current_state == AXIS_STATE_CLOSED_LOOP_CONTROL)
  {
    green = 32u;
  }
  else
  {
    blue = 32u;
  }

  if ((red != s_red) || (green != s_green) || (blue != s_blue) || s_dirty)
  {
    s_red = red;
    s_green = green;
    s_blue = blue;
    s_dirty = false;
    ws2812_set_rgb(red, green, blue);
    ws2812_update();
  }
}
