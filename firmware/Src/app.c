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

  board_init();

  /* UART first: from here on the boot path can report its progress. */
  uart_comm_init();
  /* Fibre shares the same USB device instance; initialise after the USB stack. */
  usb_fibre_init();
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
  board_watchdog_feed();
#if (BOOT_TRACE_ENABLE != 0u)
  uart_comm_printf("boot: config %s\n", config_loaded ? "loaded" : "defaults");
#endif

  axis_init(&g_axis, ENCODER_TYPE_MT6816);
  nvm_config_apply(&g_odrive_config);

  adc_register_sample_callback(axis_current_loop_callback);
  adc_start();

  can_comm_init();
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
