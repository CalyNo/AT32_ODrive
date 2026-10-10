#ifndef AT32_ODRIVE_STATUS_LED_H
#define AT32_ODRIVE_STATUS_LED_H

#include <stdint.h>

/*
 * On-board status indicator: WS2812B-2020 on PB2, driven by ws2812.c.
 *
 * status_led_task() samples the axis and pushes a new colour into the driver
 * only when that colour changed.  The colour/pattern mapping itself is pure
 * logic and lives in led_pattern.c so it can be verified on the host.
 *
 * led.mode selects where the colour comes from:
 *   LED_MODE_AUTO   follow the axis state (default)
 *   LED_MODE_MANUAL constant led.color, scaled by led.brightness
 *   LED_MODE_OFF    dark
 *
 * The task only starts a frame; ws2812_write() is non-blocking, and a frame
 * that is still on the wire is simply retried on the next scheduler tick.
 */
void status_led_init(void);
void status_led_task(void *context);

/*
 * Live LED settings, written by the led.* parameters (Src/param.c) and read on
 * every indicator tick.  They are deliberately *not* part of odrive_config_t:
 * persisting them would change the flash layout, and a NVM version bump throws
 * away the saved motor/encoder calibration.  They therefore reset to the
 * defaults on every boot.
 *
 *   mode       0 = follow the axis state, 1 = constant color, 2 = off
 *   color      0xRRGGBB, used when mode = 1
 *   brightness 0..255, scales whichever colour is active
 */
extern uint32_t g_status_led_mode;
extern uint32_t g_status_led_color;
extern uint32_t g_status_led_brightness;

/*
 * Bring-up self test: while non-zero the indicator ignores the axis and runs
 * the red/green/blue/white/dark sequence instead (led.self_test).
 */
extern uint32_t g_status_led_self_test;

/*
 * Diagnostics mirrors for the parameter table (led.busy / led.frames /
 * led.frames_done), refreshed once per indicator tick from the driver.
 * See ws2812_frames_started() for how to read them.
 */
extern uint32_t g_status_led_busy;
extern uint32_t g_status_led_frames;
extern uint32_t g_status_led_frames_done;

#endif /* AT32_ODRIVE_STATUS_LED_H */
