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

#include <stddef.h>

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
  board_clock_config();
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);
  system_time_init();

  board_init();

  pwm_init();
  adc_init();
  adc_calibrate_current_offsets();

  if (!nvm_config_load(&g_odrive_config))
  {
    nvm_config_defaults(&g_odrive_config);
    (void)nvm_config_save(&g_odrive_config);
  }

  axis_init(&g_axis, ENCODER_TYPE_MT6816);
  nvm_config_apply(&g_odrive_config);

  /* Hardware IWDG is independent of the ODrive-style communication watchdog. */
  board_watchdog_init(200u);

  adc_register_sample_callback(axis_current_loop_callback);
  adc_start();

  can_comm_init();
  uart_comm_init();
  status_led_init();

  axis_timer_init();

  scheduler_init();
  (void)scheduler_add_task(app_system_task, NULL, 1u);
  (void)scheduler_add_task(app_comm_task, NULL, 1u);
  (void)scheduler_add_task(app_calibration_task, NULL, 1u);
  (void)scheduler_add_task(app_status_task, NULL, 100u);

  uart_comm_printf("AT32_ODrive %d.%d.%d ready\n",
                   FIRMWARE_VERSION_MAJOR,
                   FIRMWARE_VERSION_MINOR,
                   FIRMWARE_VERSION_REVISION);
}

void app_run(void)
{
  scheduler_run_forever();
}
