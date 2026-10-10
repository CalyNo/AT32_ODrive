#include "led_pattern.h"

#include <stddef.h>

/*
 * Pure status-indicator logic; see Inc/led_pattern.h for the protocol and
 * pattern documentation.  Only stdint/stdbool and the pure Inc/ headers are
 * allowed here because tests/Makefile builds this file on the host.
 */

/* The high time must stay inside one bit period, otherwise the WS2812 sees a
 * constant level instead of a bit stream. */
typedef char led_ws2812_timing_check[(LED_WS2812_PERIOD_TICKS > LED_WS2812_T1H_TICKS) ? 1 : -1];
typedef char led_ws2812_clock_check[(LED_WS2812_TIMER_HZ == 288000000u) ? 1 : -1];
/* Keep the blink cap in sync with the highest axis_error_t bit (1u << 12). */
typedef char led_error_code_check[(AXIS_ERROR_TEMPERATURE_SENSOR_FAILED == (1u << 12)) ? 1 : -1];

static void led_encode_channel(uint8_t value, uint32_t *frame, uint32_t *index)
{
  uint32_t mask;

  /* WS2812 expects the most significant bit of every byte first. */
  for (mask = 0x80u; mask != 0u; mask >>= 1u)
  {
    frame[*index] = (((uint32_t)value & mask) != 0u) ? LED_WS2812_T1H_TICKS
                                                     : LED_WS2812_T0H_TICKS;
    *index += 1u;
  }
}

void led_ws2812_encode(uint8_t r, uint8_t g, uint8_t b, uint32_t *frame)
{
  uint32_t index = 0u;

  if (frame == NULL)
  {
    return;
  }

  /* On the wire the order is green, red, blue. */
  led_encode_channel(g, frame, &index);
  led_encode_channel(r, frame, &index);
  led_encode_channel(b, frame, &index);

  /* Flush slot: its transfer completes the DMA and makes the driver stop the
   * timer, leaving the line low until the next frame. */
  frame[index] = 0u;
}

uint8_t led_error_blink_code(uint32_t error)
{
  uint8_t code = 0u;
  uint32_t bit;

  if (error == 0u)
  {
    return 0u;
  }

  /* Lowest set bit wins: it is the protection that tripped first. */
  for (bit = 1u; ((error & bit) == 0u) && (bit != 0u); bit <<= 1u)
  {
    code++;
  }
  code++; /* blink codes are one-based */

  if (code > LED_ERROR_BLINK_MAX)
  {
    code = LED_ERROR_BLINK_MAX;
  }
  return code;
}

static bool led_blink_on(uint32_t time_ms,
                         uint8_t flashes,
                         uint32_t on_ms,
                         uint32_t off_ms,
                         uint32_t pause_ms)
{
  uint32_t pair;
  uint32_t active_ms;
  uint32_t cycle_ms;
  uint32_t phase_ms;

  if (flashes == 0u)
  {
    return false;
  }

  pair = on_ms + off_ms;
  active_ms = (uint32_t)flashes * pair;
  cycle_ms = active_ms + pause_ms;
  phase_ms = time_ms % cycle_ms;

  if (phase_ms >= active_ms)
  {
    return false; /* long pause between the flash trains */
  }
  return (phase_ms % pair) < on_ms;
}

led_rgb_t led_indicator_color(const led_status_t *status, uint8_t brightness)
{
  led_rgb_t colour = {0u, 0u, 0u};
  uint8_t code;
  bool on;

  if (status == NULL)
  {
    return colour;
  }

  if ((status->error != 0u) || (status->state == AXIS_STATE_ERROR))
  {
    code = led_error_blink_code(status->error);
    if (code == 0u)
    {
      /* In AXIS_STATE_ERROR without a latched bit, still show one flash. */
      code = 1u;
    }
    on = led_blink_on(status->time_ms, code, LED_ERROR_ON_MS, LED_ERROR_OFF_MS,
                      LED_ERROR_PAUSE_MS);
    if (on)
    {
      colour.r = led_scale_channel(255u, brightness);
    }
    return colour;
  }

  if (status->calibrating)
  {
    on = ((status->time_ms / LED_CALIBRATION_HALF_MS) & 1u) == 0u;
    if (on)
    {
      colour.b = led_scale_channel(255u, brightness);
    }
    return colour;
  }

  if (status->state == AXIS_STATE_CLOSED_LOOP_CONTROL)
  {
    colour.g = led_scale_channel(255u, brightness);
    return colour;
  }

  /* Idle / disarmed. */
  colour.b = led_scale_channel(255u, brightness);
  return colour;
}

led_rgb_t led_self_test_color(uint32_t time_ms, uint8_t brightness)
{
  led_rgb_t colour = {0u, 0u, 0u};
  uint8_t level = led_scale_channel(255u, brightness);

  switch ((time_ms / LED_SELF_TEST_STEP_MS) % LED_SELF_TEST_STEPS)
  {
    case 0u:
      colour.r = level;
      break;
    case 1u:
      colour.g = level;
      break;
    case 2u:
      colour.b = level;
      break;
    case 3u:
      colour.r = level;
      colour.g = level;
      colour.b = level;
      break;
    default:
      break; /* dark step: proves the frame is actually clearing the LED */
  }
  return colour;
}
