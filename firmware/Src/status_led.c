#include "status_led.h"

#include "axis.h"
#include "config.h"
#include "led_pattern.h"
#include "system_time.h"
#include "ws2812.h"

#include <stdbool.h>

/* Live LED settings; see Inc/status_led.h.  Written by the led.* parameters,
 * read on every tick, reset to the defaults by a reboot. */
uint32_t g_status_led_mode = (uint32_t)LED_MODE_AUTO;
uint32_t g_status_led_color = STATUS_LED_DEFAULT_COLOR;
uint32_t g_status_led_brightness = STATUS_LED_DEFAULT_BRIGHTNESS;
uint32_t g_status_led_self_test = 0u;

uint32_t g_status_led_busy = 0u;
uint32_t g_status_led_frames = 0u;
uint32_t g_status_led_frames_done = 0u;

/* Last colour handed to the driver, so idle ticks do not resend frames. */
static led_rgb_t s_last_colour = {0u, 0u, 0u};
static bool s_dirty = true;

static bool status_led_same_colour(led_rgb_t a, led_rgb_t b)
{
  return (a.r == b.r) && (a.g == b.g) && (a.b == b.b);
}

static led_mode_t status_led_current_mode(void)
{
  if (g_status_led_mode > (uint32_t)LED_MODE_OFF)
  {
    return LED_MODE_AUTO;
  }
  return (led_mode_t)g_status_led_mode;
}

static uint8_t status_led_current_brightness(void)
{
  if (g_status_led_brightness > 255u)
  {
    return 255u;
  }
  return (uint8_t)g_status_led_brightness;
}

void status_led_init(void)
{
  ws2812_init();

  /* Start dark; the first task tick paints the current state. */
  s_last_colour.r = 0u;
  s_last_colour.g = 0u;
  s_last_colour.b = 0u;
  s_dirty = true;
}

void status_led_task(void *context)
{
  uint8_t brightness = status_led_current_brightness();
  uint32_t color = g_status_led_color & 0x00FFFFFFu;
  led_status_t status;
  led_rgb_t colour;

  (void)context;

  /* Publish the driver diagnostics for the led.* parameter rows. */
  g_status_led_busy = ws2812_busy() ? 1u : 0u;
  g_status_led_frames = ws2812_frames_started();
  g_status_led_frames_done = ws2812_frames_completed();

  status.error = g_axis.error;
  status.state = (int32_t)g_axis.current_state;
  status.calibrating = g_axis.calibration_busy;
  status.time_ms = system_millis();

  if (g_status_led_self_test != 0u)
  {
    colour = led_self_test_color(status.time_ms, brightness);
  }
  else
  {
    switch (status_led_current_mode())
    {
      case LED_MODE_OFF:
        colour.r = 0u;
        colour.g = 0u;
        colour.b = 0u;
        break;

      case LED_MODE_MANUAL:
        colour.r = led_scale_channel((uint8_t)((color >> 16) & 0xFFu), brightness);
        colour.g = led_scale_channel((uint8_t)((color >> 8) & 0xFFu), brightness);
        colour.b = led_scale_channel((uint8_t)(color & 0xFFu), brightness);
        break;

      default:
        colour = led_indicator_color(&status, brightness);
        break;
    }
  }

  if (!s_dirty && status_led_same_colour(colour, s_last_colour))
  {
    return;
  }

  if (!ws2812_write(colour.r, colour.g, colour.b))
  {
    return; /* previous frame still on the wire; retry on the next tick */
  }

  s_last_colour = colour;
  s_dirty = false;
}
