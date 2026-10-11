#include "app.h"
#include "adc.h"
#include "axis.h"
#include "board.h"
#include "calibration.h"
#include "can_comm.h"
#include "config.h"
#include "nvm_config.h"
#include "pwm.h"
#include "scheduler.h"
#include "status_led.h"
#include "system_time.h"
#include "uart_comm.h"
#include "usb_fibre.h"

#if (BOOT_MARKER_ENABLE != 0u)
#include "led_pattern.h"
#include "ws2812.h"
#endif

#include <stdbool.h>
#include <stddef.h>

/*
 * Boot path
 * ---------
 * The IWDG keeps counting across a system reset (only a power-on reset stops
 * it) and, once enabled, it can never be disabled again.  Enabling it in the
 * middle of app_init -- as this firmware used to do -- turns any hang in the
 * remaining init steps (a silent clock/ADC wait, a firmware fault) into a
 * permanent reset loop: the chip re-enters app_init, hangs again, and is reset
 * again 200 ms later, so the failure looks like "stuck in app_init".
 *
 * The order below therefore is:
 *   1. read the reset cause, so a loop can actually be explained;
 *   2. feed the dog once, to re-arm a counter left running by a previous run;
 *   3. bring up the UART early, so the boot path can report where it stops;
 *   4. only *enable* the IWDG at the very end, once the init work that can
 *      block is done.  A hang before that point stays a hang and is visible in
 *      a debugger instead of being masked by a reset loop.
 */
#if (BOOT_TRACE_ENABLE != 0u)
static const char *app_reset_cause_text(uint32_t cause)
{
  /* Most interesting cause first: an IWDG reset is what a boot loop with a
   * live watchdog looks like. */
  if ((cause & BOARD_RESET_CAUSE_WATCHDOG) != 0u)
  {
    return "IWDG";
  }
  if ((cause & BOARD_RESET_CAUSE_WINDOW_WATCHDOG) != 0u)
  {
    return "WWDG";
  }
  if ((cause & BOARD_RESET_CAUSE_SOFTWARE) != 0u)
  {
    return "SW";
  }
  if ((cause & BOARD_RESET_CAUSE_LOW_POWER) != 0u)
  {
    return "LP";
  }
  if ((cause & BOARD_RESET_CAUSE_NRST) != 0u)
  {
    return "NRST";
  }
  if ((cause & BOARD_RESET_CAUSE_POR) != 0u)
  {
    return "POR";
  }
  return "NONE";
}
#endif /* BOOT_TRACE_ENABLE */

#if (BOOT_MARKER_ENABLE != 0u)
/*
 * Boot checkpoints on the status LED (BOOT_MARKER_ENABLE)
 * ------------------------------------------------------
 * On an unproven board the only output can be the WS2812 on PB2: no debugger,
 * no serial adapter.  Every marker is "the stage number, twice, in the colour
 * that reports the clock state":
 *
 *   green = intended 288 MHz HEXT+PLL clock, red = degraded HICK fallback
 *   N blinks, a longer gap, N blinks again, then dark
 *
 * so "3 blinks then darkness" reads as "the boot hung after checkpoint 3".
 * The markers must never be the reason the boot stops, so nothing here waits
 * without a bound: every wait counts a volatile loop counter and gives up, so
 * a marker on a dead WS2812/DMA/interrupt chain still costs only a fixed time.
 * The LED brightness is the status-LED default, i.e. the same as led.brightness.
 */
#define BOOT_MARKER_BRIGHTNESS   STATUS_LED_DEFAULT_BRIGHTNESS

/* Time between two frames: far longer than the >= 50 us reset/latch time a
 * WS2812B needs between frames. */
#define BOOT_MARKER_FRAME_MS     (20u)
/* Upper bound of one busy-wait iteration budget.  At 288 MHz one iteration of
 * boot_marker_delay_loops() costs a few cycles, so a budget of ~250000 is a
 * few ms: enough to cover a handful of timer frames even if the very first one
 * was already in flight, and short enough to be irrelevant on a dead chain. */
#define BOOT_MARKER_DELAY_LOOPS  (250000u)
#define BOOT_MARKER_WAIT_LOOPS   (BOOT_MARKER_FRAME_MS * BOOT_MARKER_DELAY_LOOPS)

#define BOOT_MARKER_BLINK_MS     (350u)
#define BOOT_MARKER_BLINK_GAP_MS (280u)
#define BOOT_MARKER_GROUP_GAP_MS (1200u)
#define BOOT_MARKER_TAIL_MS      (800u)

#define BOOT_MARKER_COLOR_DARK   (0u)
#define BOOT_MARKER_COLOR_GREEN  (0x0000FF00u)
#define BOOT_MARKER_COLOR_RED    (0x00FF0000u)

/*
 * Bounded busy wait.  system_delay_ms() cannot be used for the first marker:
 * it waits on the SysTick counter that system_time_init() sets up, which runs
 * after board_clock_config(), so before that point it would spin forever.  The
 * counter is volatile and read by the loop condition, which keeps the loop.
 */
static void boot_marker_delay_loops(uint32_t loops)
{
  volatile uint32_t remaining = loops;

  while (remaining != 0u)
  {
    remaining--;
  }
}

/*
 * Push one colour and wait -- with a bound -- until the frame has left the
 * wire, so the next frame does not run into ws2812_write() returning false.
 * The DMA1_CH1 interrupt is expected to clear the busy flag; if it never
 * fires (the very fault this instrumentation is meant to expose) the wait
 * gives up after BOOT_MARKER_FRAME_MS and the boot carries on.
 */
static void boot_marker_frame(uint32_t color)
{
  uint32_t budget = BOOT_MARKER_WAIT_LOOPS;

  while (ws2812_busy() && (budget != 0u))
  {
    boot_marker_delay_loops(BOOT_MARKER_DELAY_LOOPS);
    budget--;
  }

  if (!ws2812_write(led_scale_channel((uint8_t)((color >> 16) & 0xFFu), BOOT_MARKER_BRIGHTNESS),
                    led_scale_channel((uint8_t)((color >> 8) & 0xFFu), BOOT_MARKER_BRIGHTNESS),
                    led_scale_channel((uint8_t)(color & 0xFFu), BOOT_MARKER_BRIGHTNESS)))
  {
    return; /* frame is stuck on the wire: keep the previous colour and move on */
  }

  budget = BOOT_MARKER_WAIT_LOOPS;
  while (ws2812_busy() && (budget != 0u))
  {
    boot_marker_delay_loops(BOOT_MARKER_DELAY_LOOPS);
    budget--;
  }
}

static void boot_marker_hold(uint32_t color, uint32_t ms)
{
  uint32_t ticks = ms / BOOT_MARKER_FRAME_MS;

  if (ticks == 0u)
  {
    ticks = 1u;
  }

  while (ticks != 0u)
  {
    boot_marker_frame(color);
    ticks--;
  }
}

static void boot_marker_blink(uint32_t color, uint32_t stage)
{
  uint32_t group;
  uint32_t blink;

  for (group = 0u; group < 2u; group++)
  {
    for (blink = 0u; blink < stage; blink++)
    {
      boot_marker_hold(color, BOOT_MARKER_BLINK_MS);
      boot_marker_hold(BOOT_MARKER_COLOR_DARK, BOOT_MARKER_BLINK_GAP_MS);
    }

    /* The gap between the two groups must be clearly longer than a blink gap,
     * otherwise "3 then 3" cannot be counted reliably by eye. */
    if (group == 0u)
    {
      boot_marker_hold(BOOT_MARKER_COLOR_DARK, BOOT_MARKER_GROUP_GAP_MS);
    }
  }

  /* End dark, so the pattern reads as "stage N blinks, then nothing". */
  boot_marker_hold(BOOT_MARKER_COLOR_DARK, BOOT_MARKER_TAIL_MS);
}

/*
 * One boot checkpoint.  Must not be called before board_clock_config(): the bit
 * timing assumes the 288 MHz timer clock, so an earlier marker would send a
 * waveform no WS2812 can decode.  Call it immediately after that point.
 */
static void app_boot_marker(uint32_t stage)
{
  if (!board_clock_is_degraded())
  {
    boot_marker_blink(BOOT_MARKER_COLOR_GREEN, stage);
  }
  else
  {
    boot_marker_blink(BOOT_MARKER_COLOR_RED, stage);
  }
}
#endif /* BOOT_MARKER_ENABLE */

static void app_system_task(void *context)
{
  (void)context;
  board_watchdog_feed();
  axis_update_temperatures(&g_axis);
  axis_communication_watchdog_update(&g_axis, system_millis());
}

static void app_comm_task(void *context)
{
  (void)context;
  can_comm_poll();
  uart_comm_poll();
  usb_fibre_poll();
}

static void app_calibration_task(void *context)
{
  (void)context;
  if (g_axis.calibration_busy)
  {
    (void)calibration_process(&g_axis);
  }
}

static void app_status_task(void *context)
{
  status_led_task(context);
}

void app_init(void)
{
  uint32_t reset_cause;
  bool config_loaded;

  reset_cause = board_reset_cause_take();
  board_watchdog_feed();

  board_clock_config();
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);
  system_time_init();
#if (BOOT_MARKER_ENABLE != 0u)
  /*
   * Earliest useful checkpoint: the WS2812 bit timing is derived from the
   * 288 MHz timer clock, so a marker before board_clock_config() could not be
   * decoded.  This first one also reports the clock state by colour.
   */
  ws2812_init();
  app_boot_marker(1u);
#endif

  board_init();
#if (BOOT_MARKER_ENABLE != 0u)
  app_boot_marker(2u);
#endif

  /* UART first: from here on the boot path can report its progress. */
  uart_comm_init();
#if (BOOT_MARKER_ENABLE != 0u)
  /* uart_comm_init() is where usb_cdc_init() runs. */
  app_boot_marker(3u);
#endif
  /* Fibre shares the same USB device instance; initialise after the USB stack. */
  usb_fibre_init();
#if (BOOT_MARKER_ENABLE != 0u)
  app_boot_marker(4u);
#endif
#if (BOOT_TRACE_ENABLE != 0u)
  uart_comm_printf("boot: reset=%s (0x%02X) clock=%s\n",
                   app_reset_cause_text(reset_cause),
                   reset_cause,
                   board_clock_is_degraded() ? "DEGRADED(HICK)" : "HEXT+PLL 288MHz");
#else
  (void)reset_cause;
#endif

  pwm_init();
  adc_init();
  adc_calibrate_current_offsets();
#if (BOOT_MARKER_ENABLE != 0u)
  app_boot_marker(5u);
#endif
  board_watchdog_feed();
#if (BOOT_TRACE_ENABLE != 0u)
  uart_comm_printf("boot: clock+gpio+adc ok\n");
#endif

  config_loaded = nvm_config_load(&g_odrive_config);
  if (!config_loaded)
  {
    nvm_config_defaults(&g_odrive_config);
    (void)nvm_config_save(&g_odrive_config);
  }
#if (BOOT_MARKER_ENABLE != 0u)
  app_boot_marker(6u);
#endif
  board_watchdog_feed();
#if (BOOT_TRACE_ENABLE != 0u)
  uart_comm_printf("boot: config %s\n", config_loaded ? "loaded" : "defaults");
#endif

  axis_init(&g_axis, ENCODER_TYPE_MT6816);
  nvm_config_apply(&g_odrive_config);
#if (BOOT_MARKER_ENABLE != 0u)
  app_boot_marker(7u);
#endif

  adc_register_sample_callback(axis_current_loop_callback);
  adc_start();

  can_comm_init();
#if (BOOT_MARKER_ENABLE != 0u)
  /* Last checkpoint: status_led_init() takes the LED over from here on. */
  app_boot_marker(8u);
#endif
  status_led_init();

  axis_timer_init();

  scheduler_init();
  (void)scheduler_add_task(app_system_task, NULL, 1u);
  (void)scheduler_add_task(app_comm_task, NULL, 1u);
  (void)scheduler_add_task(app_calibration_task, NULL, 1u);
  (void)scheduler_add_task(app_status_task, NULL, 100u);

  /*
   * Everything that can block is behind us: arm the hardware watchdog for the
   * runtime.  app_system_task() feeds it from the first scheduler tick on.
   */
  board_watchdog_init(200u);

  uart_comm_printf("AT32_ODrive %d.%d.%d ready\n",
                   FIRMWARE_VERSION_MAJOR,
                   FIRMWARE_VERSION_MINOR,
                   FIRMWARE_VERSION_REVISION);
}

void app_run(void)
{
  scheduler_run_forever();
}
