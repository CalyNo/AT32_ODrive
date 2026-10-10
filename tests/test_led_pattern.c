#include "led_pattern.h"

#include <stdio.h>

static int check(int condition, const char *what)
{
  if (!condition)
  {
    printf("FAIL %s\n", what);
    return 0;
  }
  return 1;
}

static int same_colour(led_rgb_t a, uint8_t r, uint8_t g, uint8_t b)
{
  return (a.r == r) && (a.g == g) && (a.b == b);
}

static int test_encoding(void)
{
  uint32_t frame[LED_WS2812_FRAME_ENTRIES];
  uint32_t i;

  /* Bit timing derived from the 288 MHz APB2 timer clock. */
  if (!check(LED_WS2812_PERIOD_TICKS == 360u, "period ticks")) return 0;
  if (!check(LED_WS2812_PERIOD_RELOAD == 359u, "period reload")) return 0;
  if (!check(LED_WS2812_T0H_TICKS == 100u, "T0H ticks")) return 0;
  if (!check(LED_WS2812_T1H_TICKS == 201u, "T1H ticks")) return 0;

  /* Black: every bit slot is a zero bit, the flush slot is dark. */
  led_ws2812_encode(0u, 0u, 0u, frame);
  for (i = 0u; i < 24u; i++)
  {
    if (!check(frame[i] == LED_WS2812_T0H_TICKS, "black bit")) return 0;
  }
  if (!check(frame[24] == 0u, "flush slot")) return 0;

  /* White: every bit slot is a one bit. */
  led_ws2812_encode(0xFFu, 0xFFu, 0xFFu, frame);
  for (i = 0u; i < 24u; i++)
  {
    if (!check(frame[i] == LED_WS2812_T1H_TICKS, "white bit")) return 0;
  }

  /* Wire order is green, red, blue. */
  led_ws2812_encode(0xFFu, 0x00u, 0x00u, frame);
  for (i = 0u; i < 8u; i++)
  {
    if (!check(frame[i] == LED_WS2812_T0H_TICKS, "green slot")) return 0;
  }
  for (i = 8u; i < 16u; i++)
  {
    if (!check(frame[i] == LED_WS2812_T1H_TICKS, "red slot")) return 0;
  }
  for (i = 16u; i < 24u; i++)
  {
    if (!check(frame[i] == LED_WS2812_T0H_TICKS, "blue slot")) return 0;
  }

  /* Most significant bit first: red = 0x01 must only pulse in its last bit. */
  led_ws2812_encode(0x01u, 0x00u, 0x00u, frame);
  for (i = 8u; i < 15u; i++)
  {
    if (!check(frame[i] == LED_WS2812_T0H_TICKS, "red MSB-first low")) return 0;
  }
  if (!check(frame[15] == LED_WS2812_T1H_TICKS, "red LSB high")) return 0;

  /* NULL frame must not fault. */
  led_ws2812_encode(1u, 2u, 3u, NULL);

  return 1;
}

static int test_error_code(void)
{
  uint8_t code;

  if (!check(led_error_blink_code(0u) == 0u, "no error -> no flash")) return 0;

  code = led_error_blink_code(AXIS_ERROR_INVALID_STATE);
  if (!check(code == 1u, "invalid state code")) return 0;
  code = led_error_blink_code(AXIS_ERROR_DC_BUS_OVER_VOLTAGE);
  if (!check(code == 2u, "bus over voltage code")) return 0;
  code = led_error_blink_code(AXIS_ERROR_ESTOP_REQUESTED);
  if (!check(code == 12u, "estop code")) return 0;
  code = led_error_blink_code(AXIS_ERROR_TEMPERATURE_SENSOR_FAILED);
  if (!check(code == 13u, "temperature sensor code")) return 0;

  /* The lowest set bit wins: it tripped first. */
  code = led_error_blink_code(AXIS_ERROR_DC_BUS_OVER_VOLTAGE | AXIS_ERROR_ENCODER_FAILED);
  if (!check(code == 2u, "lowest bit wins")) return 0;

  return 1;
}

static int test_auto_colours(void)
{
  led_status_t status;
  led_rgb_t colour;

  status.error = 0u;
  status.calibrating = false;
  status.time_ms = 0u;

  /* Brightness 0 must always be dark, whatever the state. */
  status.state = AXIS_STATE_CLOSED_LOOP_CONTROL;
  colour = led_indicator_color(&status, 0u);
  if (!check(same_colour(colour, 0u, 0u, 0u), "brightness 0 dark")) return 0;

  /* Idle / disarmed is blue, closed loop is green. */
  status.state = AXIS_STATE_IDLE;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 0u, 255u), "idle blue")) return 0;

  status.state = AXIS_STATE_CLOSED_LOOP_CONTROL;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 255u, 0u), "closed loop green")) return 0;

  /* Calibration blinks blue, 400 ms on / 400 ms off. */
  status.state = AXIS_STATE_IDLE;
  status.calibrating = true;
  status.time_ms = 0u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 0u, 255u), "calibration on")) return 0;
  status.time_ms = 399u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 0u, 255u), "calibration on end")) return 0;
  status.time_ms = 400u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 0u, 0u), "calibration off")) return 0;
  status.time_ms = 800u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 0u, 255u), "calibration restarts")) return 0;

  return 1;
}

static int test_error_colours(void)
{
  led_status_t status;
  led_rgb_t colour;

  status.error = AXIS_ERROR_DC_BUS_OVER_VOLTAGE; /* two flashes */
  status.state = AXIS_STATE_IDLE;
  status.calibrating = false;

  /* Flash train: 250 ms on / 250 ms off, twice, then a 1500 ms pause. */
  status.time_ms = 0u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "error flash 1 on")) return 0;
  status.time_ms = 249u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "error flash 1 on end")) return 0;
  status.time_ms = 250u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 0u, 0u), "error gap")) return 0;
  status.time_ms = 500u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "error flash 2 on")) return 0;
  status.time_ms = 1000u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 0u, 0u), "error pause")) return 0;
  status.time_ms = 2499u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 0u, 0u, 0u), "error pause end")) return 0;
  status.time_ms = 2500u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "error repeats")) return 0;

  /* An error bit outranks the closed loop colour. */
  status.state = AXIS_STATE_CLOSED_LOOP_CONTROL;
  status.time_ms = 0u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "error outranks closed loop")) return 0;

  /* AXIS_STATE_ERROR without a latched bit still flashes once. */
  status.error = 0u;
  status.state = AXIS_STATE_ERROR;
  status.time_ms = 0u;
  colour = led_indicator_color(&status, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "error state flashes")) return 0;

  return 1;
}

static int test_brightness_scale(void)
{
  led_status_t status;
  led_rgb_t colour;

  if (!check(led_scale_channel(0u, 255u) == 0u, "scale 0")) return 0;
  if (!check(led_scale_channel(255u, 255u) == 255u, "scale full")) return 0;
  if (!check(led_scale_channel(255u, 128u) == 128u, "scale half")) return 0;
  if (!check(led_scale_channel(255u, 0u) == 0u, "scale dark")) return 0;
  if (!check(led_scale_channel(128u, 64u) == 32u, "scale quarter")) return 0;

  status.error = 0u;
  status.calibrating = false;
  status.time_ms = 0u;
  status.state = AXIS_STATE_IDLE;
  colour = led_indicator_color(&status, 64u);
  if (!check(same_colour(colour, 0u, 0u, 64u), "idle at 25 percent")) return 0;

  /* A NULL snapshot must be handled instead of dereferenced. */
  colour = led_indicator_color(NULL, 255u);
  if (!check(same_colour(colour, 0u, 0u, 0u), "null status")) return 0;

  return 1;
}

static int test_self_test(void)
{
  led_rgb_t colour;

  /* red -> green -> blue -> white -> dark, 500 ms per step. */
  colour = led_self_test_color(0u, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "self test red")) return 0;
  colour = led_self_test_color(499u, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "self test red end")) return 0;
  colour = led_self_test_color(500u, 255u);
  if (!check(same_colour(colour, 0u, 255u, 0u), "self test green")) return 0;
  colour = led_self_test_color(1000u, 255u);
  if (!check(same_colour(colour, 0u, 0u, 255u), "self test blue")) return 0;
  colour = led_self_test_color(1500u, 255u);
  if (!check(same_colour(colour, 255u, 255u, 255u), "self test white")) return 0;
  colour = led_self_test_color(2000u, 255u);
  if (!check(same_colour(colour, 0u, 0u, 0u), "self test dark")) return 0;
  colour = led_self_test_color(2500u, 255u);
  if (!check(same_colour(colour, 255u, 0u, 0u), "self test repeats")) return 0;

  /* Brightness scales the whole sequence. */
  colour = led_self_test_color(0u, 64u);
  if (!check(same_colour(colour, 64u, 0u, 0u), "self test scaled")) return 0;

  return 1;
}

int main(void)
{
  if (!test_encoding()) return 1;
  if (!test_error_code()) return 1;
  if (!test_auto_colours()) return 1;
  if (!test_error_colours()) return 1;
  if (!test_brightness_scale()) return 1;
  if (!test_self_test()) return 1;

  printf("PASS test_led_pattern\n");
  return 0;
}
