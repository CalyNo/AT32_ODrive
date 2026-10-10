#include "param.h"

#include "adc.h"
#include "axis.h"
#include "board.h"
#include "config.h"
#include "nvm_config.h"
#include "status_led.h"
#include "uart_comm.h"
#include "usb_cdc.h"
#include "util.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Owned by the CMSIS device file; published as board.core_clock. */
extern unsigned int system_core_clock;

/*
 * Sentinel bounds for rows that accept any finite value, matching the
 * behaviour they had before the table existed.
 */
#define PARAM_UNBOUNDED   (3.0e38f)

/* Firmware version is reported through the parameter namespace but is not a
 * variable, so give param_print() something to point at. */
static const int32_t s_fw_version_major = (int32_t)FIRMWARE_VERSION_MAJOR;
static const int32_t s_fw_version_minor = (int32_t)FIRMWARE_VERSION_MINOR;

/* -------------------------------------------------------------------------
 * Value loads
 *
 * All loads go through memcpy() so a row may point at any scalar type without
 * violating strict aliasing (several rows point at enum-typed fields).
 * ------------------------------------------------------------------------- */

static float param_load_f32(const void *ptr)
{
  float value;
  memcpy(&value, ptr, sizeof(value));
  return value;
}

static int32_t param_load_i32(const void *ptr)
{
  int32_t value;
  memcpy(&value, ptr, sizeof(value));
  return value;
}

static uint32_t param_load_u32(const void *ptr)
{
  uint32_t value;
  memcpy(&value, ptr, sizeof(value));
  return value;
}

static uint32_t param_load_u16(const void *ptr)
{
  uint16_t value;
  memcpy(&value, ptr, sizeof(value));
  return (uint32_t)value;
}

/* -------------------------------------------------------------------------
 * Extra write handling
 * ------------------------------------------------------------------------- */

static void param_on_write_requested_state(const param_entry_t *entry, float value)
{
  (void)entry;
  axis_set_requested_state(&g_axis, (axis_state_t)((int32_t)value));
}

static void param_on_write_error(const param_entry_t *entry, float value)
{
  (void)entry;
  g_axis.error = (uint32_t)(int32_t)value;
  axis_set_requested_state(&g_axis, AXIS_STATE_IDLE);
}

static void param_on_write_clear_errors(const param_entry_t *entry, float value)
{
  (void)entry;
  (void)value;
  g_axis.error = AXIS_ERROR_NONE;
  axis_set_requested_state(&g_axis, AXIS_STATE_IDLE);
}

static void param_on_write_input_pos(const param_entry_t *entry, float value)
{
  (void)entry;
  axis_set_input_pos(&g_axis, value);
}

static void param_on_write_input_vel(const param_entry_t *entry, float value)
{
  (void)entry;
  axis_set_input_vel(&g_axis, value);
}

static void param_on_write_input_torque(const param_entry_t *entry, float value)
{
  (void)entry;
  axis_set_input_torque(&g_axis, value);
}

/* current_lim is exposed twice in the ODrive namespace; keep the controller
 * limit view in step so nvm_config_apply() lowers the torque ceiling too. */
static void param_on_write_current_lim(const param_entry_t *entry, float value)
{
  (void)entry;
  g_odrive_config.controller.current_limit = value;
}

/* Boolean configuration fields are stored as 0/1 whatever the caller sent. */
static void param_on_write_normalize_u32(const param_entry_t *entry, float value)
{
  *(uint32_t *)entry->config_ptr = (value != 0.0f) ? 1u : 0u;
}

/*
 * CPR and pole pairs define the mechanical-to-electrical mapping that the
 * encoder offset calibration was measured against, so changing either one
 * invalidates that calibration.  Runs before nvm_config_apply(), and also
 * drops the saved pre_calibrated flag so apply() cannot re-arm it from the
 * stale stored offset -- CLOSED_LOOP_CONTROL stays blocked until the offset is
 * measured again.
 */
static void param_on_write_invalidate_encoder_offset(const param_entry_t *entry, float value)
{
  (void)entry;
  (void)value;
  g_axis.encoder_offset_valid = false;
  g_odrive_config.encoder.pre_calibrated = 0u;
}

/*
 * The status indicator settings are runtime-only (see Inc/status_led.h), so
 * they have no odrive_config_t field to store into: the row's read pointer
 * doubles as the storage and this handler writes it.
 */
static void param_on_write_led(const param_entry_t *entry, float value)
{
  if (entry->read_ptr == &g_status_led_mode)
  {
    g_status_led_mode = (uint32_t)value;
  }
  else if (entry->read_ptr == &g_status_led_color)
  {
    g_status_led_color = (uint32_t)value;
  }
  else if (entry->read_ptr == &g_status_led_brightness)
  {
    g_status_led_brightness = (uint32_t)value;
  }
  else if (entry->read_ptr == &g_status_led_self_test)
  {
    g_status_led_self_test = (uint32_t)value;
  }
  else
  {
    /* Nothing to do: the table row and this handler have drifted apart. */
  }
}

/* CAN 120 ohm termination switch (PC13, see board_can_termination_set()). */
static uint32_t s_can_termination;

static void param_on_write_can_termination(const param_entry_t *entry, float value)
{
  (void)entry;
  s_can_termination = (value != 0.0f) ? 1u : 0u;
  board_can_termination_set(s_can_termination != 0u);
}

/* -------------------------------------------------------------------------
 * Parameter table
 *
 * Columns: path, alias, kind, precision, read_ptr, config_ptr, on_write,
 *          min_value, max_value, clamp
 * ------------------------------------------------------------------------- */

static const param_entry_t s_params[] =
{
  { "fw_version_major", NULL, PARAM_KIND_I32, 0u, &s_fw_version_major, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "fw_version_minor", NULL, PARAM_KIND_I32, 0u, &s_fw_version_minor, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "vbus_voltage", NULL, PARAM_KIND_F32, 3u, &g_axis.vbus_voltage, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },

  { "axis0.error", NULL, PARAM_KIND_U32, 0u, &g_axis.error, NULL, param_on_write_error,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.last_error", NULL, PARAM_KIND_U32, 0u, &g_axis.last_error, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.last_error_time_ms", NULL, PARAM_KIND_U32, 0u, &g_axis.last_error_time_ms, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.current_state", NULL, PARAM_KIND_I32, 0u, &g_axis.current_state, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.requested_state", "axis0.config.requested_state", PARAM_KIND_I32, 0u,
    &g_axis.requested_state, NULL, param_on_write_requested_state, 0.0f, 15.0f, true },
  { "axis0.clear_errors", "axis0.clear_errors()", PARAM_KIND_I32, 0u, NULL, NULL,
    param_on_write_clear_errors,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },

  { "axis0.motor.config.pole_pairs", "axis0.config.motor.pole_pairs", PARAM_KIND_I32, 0u,
    &g_odrive_config.motor.pole_pairs, &g_odrive_config.motor.pole_pairs,
    param_on_write_invalidate_encoder_offset, 1.0f, 64.0f, false },
  { "axis0.motor.config.current_lim", "axis0.config.motor.current_lim", PARAM_KIND_F32, 4u,
    &g_odrive_config.motor.current_lim, &g_odrive_config.motor.current_lim,
    param_on_write_current_lim, 0.0f, PARAM_UNBOUNDED, true },
  { "axis0.motor.config.calibration_current", "axis0.config.motor.calibration_current",
    PARAM_KIND_F32, 4u, &g_odrive_config.motor.calibration_current,
    &g_odrive_config.motor.calibration_current, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.config.torque_constant", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.motor.torque_constant, &g_odrive_config.motor.torque_constant, NULL,
    0.0f, PARAM_UNBOUNDED, false },
  { "axis0.motor.config.r_wl_ff_enable", NULL, PARAM_KIND_U32, 0u,
    &g_odrive_config.motor.r_wl_ff_enable, &g_odrive_config.motor.r_wl_ff_enable,
    param_on_write_normalize_u32, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.config.bemf_ff_enable", NULL, PARAM_KIND_U32, 0u,
    &g_odrive_config.motor.bemf_ff_enable, &g_odrive_config.motor.bemf_ff_enable,
    param_on_write_normalize_u32, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.phase_resistance", NULL, PARAM_KIND_F32, 6u, &g_axis.phase_resistance, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.phase_inductance", NULL, PARAM_KIND_F32, 9u, &g_axis.phase_inductance, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.current_control.Id_setpoint", NULL, PARAM_KIND_F32, 4u, &g_axis.i_d_setpoint,
    NULL, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.current_control.Iq_setpoint", NULL, PARAM_KIND_F32, 4u, &g_axis.i_q_setpoint,
    NULL, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },

  /* Temperature inputs: PB1 (on-board NTC) drives the over-temperature trip,
   * PB0 is the optional off-board sensor.  Names match ODrive 0.5. */
  { "axis0.motor.fet_thermistor.temperature", NULL, PARAM_KIND_F32, 1u, &g_axis.temp_mos,
    NULL, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.fet_thermistor.raw", NULL, PARAM_KIND_U16, 0u, &g_axis.temp_mos_raw,
    NULL, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.motor_thermistor.temperature", NULL, PARAM_KIND_F32, 1u, &g_axis.temp_motor,
    NULL, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.motor.motor_thermistor.raw", NULL, PARAM_KIND_U16, 0u, &g_axis.temp_motor_raw,
    NULL, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },

  { "axis0.encoder.config.cpr", "axis0.config.encoder.cpr", PARAM_KIND_F32, 4u,
    &g_odrive_config.encoder.cpr, &g_odrive_config.encoder.cpr,
    param_on_write_invalidate_encoder_offset, 0.0f, PARAM_UNBOUNDED, false },
  { "axis0.encoder.config.bandwidth", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.encoder.bandwidth, &g_odrive_config.encoder.bandwidth, NULL,
    0.0f, PARAM_UNBOUNDED, false },
  { "axis0.encoder.pos_estimate", NULL, PARAM_KIND_F32, 4u, &g_axis.pos_estimate, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.encoder.vel_estimate", NULL, PARAM_KIND_F32, 4u, &g_axis.vel_estimate, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.encoder.shadow_count", NULL, PARAM_KIND_I32, 0u, &g_axis.encoder.shadow_count, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.encoder.raw", NULL, PARAM_KIND_U16, 0u, &g_axis.encoder.raw, NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },

  { "axis0.controller.input_pos", NULL, PARAM_KIND_F32, 4u, &g_axis.controller.pos_input, NULL,
    param_on_write_input_pos, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.controller.input_vel", NULL, PARAM_KIND_F32, 4u, &g_axis.controller.vel_input, NULL,
    param_on_write_input_vel, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.controller.input_torque", NULL, PARAM_KIND_F32, 4u, &g_axis.controller.torque_input,
    NULL, param_on_write_input_torque, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.controller.control_mode", "axis0.controller.config.control_mode", PARAM_KIND_I32, 0u,
    &g_odrive_config.controller.control_mode, &g_odrive_config.controller.control_mode, NULL,
    0.0f, 3.0f, true },
  { "axis0.controller.input_mode", "axis0.controller.config.input_mode", PARAM_KIND_I32, 0u,
    &g_odrive_config.controller.input_mode, &g_odrive_config.controller.input_mode, NULL,
    0.0f, 8.0f, true },
  { "axis0.controller.pos_setpoint", NULL, PARAM_KIND_F32, 4u, &g_axis.controller.pos_setpoint,
    NULL, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.controller.vel_setpoint", NULL, PARAM_KIND_F32, 4u, &g_axis.controller.vel_setpoint,
    NULL, NULL, -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.controller.config.pos_gain", "axis0.config.controller.pos_gain", PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.pos_gain, &g_odrive_config.controller.pos_gain, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.controller.config.vel_gain", "axis0.config.controller.vel_gain", PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.vel_gain, &g_odrive_config.controller.vel_gain, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.controller.config.vel_integrator_gain", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.vel_integrator_gain,
    &g_odrive_config.controller.vel_integrator_gain, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "axis0.controller.config.vel_limit", NULL, PARAM_KIND_F32, 4u, &g_axis.controller.vel_limit,
    &g_odrive_config.controller.vel_limit, NULL, 0.0f, PARAM_UNBOUNDED, false },
  { "axis0.controller.config.vel_ramp_rate", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.vel_ramp_rate, &g_odrive_config.controller.vel_ramp_rate, NULL,
    0.0f, PARAM_UNBOUNDED, true },
  { "axis0.controller.config.torque_ramp_rate", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.torque_ramp_rate, &g_odrive_config.controller.torque_ramp_rate, NULL,
    0.0f, PARAM_UNBOUNDED, true },
  { "axis0.controller.config.input_filter_bandwidth", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.pos_filter_bandwidth,
    &g_odrive_config.controller.pos_filter_bandwidth, NULL, 0.0f, PARAM_UNBOUNDED, false },
  { "axis0.controller.config.traj_vel_limit", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.traj_vel_limit, &g_odrive_config.controller.traj_vel_limit, NULL,
    0.0f, PARAM_UNBOUNDED, false },
  { "axis0.controller.config.traj_accel_limit", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.traj_accel_limit, &g_odrive_config.controller.traj_accel_limit, NULL,
    0.0f, PARAM_UNBOUNDED, false },
  { "axis0.controller.config.traj_decel_limit", NULL, PARAM_KIND_F32, 4u,
    &g_odrive_config.controller.traj_decel_limit, &g_odrive_config.controller.traj_decel_limit, NULL,
    0.0f, PARAM_UNBOUNDED, false },

  /* Bit rate is only applied when the CAN peripheral is initialised, i.e. after
   * a reboot -- same flow as ODrive (set, save_configuration(), reboot). */
  { "can.config.baud_rate", NULL, PARAM_KIND_U32, 0u,
    &g_odrive_config.comm.can_baudrate, &g_odrive_config.comm.can_baudrate, NULL,
    10000.0f, 1000000.0f, false },
  { "axis0.config.can.heartbeat_rate_ms", "axis0.can.heartbeat_rate_ms", PARAM_KIND_U32, 0u,
    &g_odrive_config.comm.can_heartbeat_rate_ms, &g_odrive_config.comm.can_heartbeat_rate_ms,
    NULL, 0.0f, 10000.0f, true },
  { "axis0.can.node_id", "axis0.config.can.node_id", PARAM_KIND_U32, 0u, &g_axis.can_node_id,
    &g_odrive_config.comm.can_node_id, NULL, 0.0f, 63.0f, false },

  /* Status indicator (WS2812B on PB2).  mode 0 = follow the axis state,
   * 1 = constant colour (color, 0xRRGGBB), 2 = off.  brightness is a 0..255
   * scale factor applied to whichever colour is active.  Runtime-only: see
   * param_on_write_led(). */
  { "led.mode", NULL, PARAM_KIND_U32, 0u, &g_status_led_mode, NULL, param_on_write_led,
    0.0f, 2.0f, true },
  { "led.color", NULL, PARAM_KIND_U32, 0u, &g_status_led_color, NULL, param_on_write_led,
    0.0f, 16777215.0f, true },
  { "led.brightness", NULL, PARAM_KIND_U32, 0u, &g_status_led_brightness, NULL, param_on_write_led,
    0.0f, 255.0f, true },

  /*
   * Bring-up diagnostics (see docs/bringup.md).  All read-only except
   * led.self_test; none of them are persisted.
   */
  { "led.self_test", NULL, PARAM_KIND_U32, 0u, &g_status_led_self_test, NULL, param_on_write_led,
    0.0f, 1.0f, true },
  { "led.busy", NULL, PARAM_KIND_U32, 0u, &g_status_led_busy, NULL, NULL, 0.0f, 1.0f, true },
  { "led.frames", NULL, PARAM_KIND_U32, 0u, &g_status_led_frames, NULL, NULL,
    0.0f, PARAM_UNBOUNDED, true },
  { "led.frames_done", NULL, PARAM_KIND_U32, 0u, &g_status_led_frames_done, NULL, NULL,
    0.0f, PARAM_UNBOUNDED, true },

  /* Current sense chain: raw injected samples (12-bit) and the zero-current
   * offset error measured at boot. */
  { "adc.current_raw_a", NULL, PARAM_KIND_U16, 0u, &g_adc_current_raw[0], NULL, NULL,
    0.0f, 4095.0f, true },
  { "adc.current_raw_b", NULL, PARAM_KIND_U16, 0u, &g_adc_current_raw[1], NULL, NULL,
    0.0f, 4095.0f, true },
  { "adc.current_raw_c", NULL, PARAM_KIND_U16, 0u, &g_adc_current_raw[2], NULL, NULL,
    0.0f, 4095.0f, true },
  { "adc.current_offset_a", NULL, PARAM_KIND_F32, 5u, &g_adc_current_offset[0], NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "adc.current_offset_b", NULL, PARAM_KIND_F32, 5u, &g_adc_current_offset[1], NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },
  { "adc.current_offset_c", NULL, PARAM_KIND_F32, 5u, &g_adc_current_offset[2], NULL, NULL,
    -PARAM_UNBOUNDED, PARAM_UNBOUNDED, true },

  /* CAN 120 ohm termination switch, needed when bringing up CAN with a single
   * host adapter on the bench.  Runtime-only, defaults to off. */
  { "can.termination", NULL, PARAM_KIND_U32, 0u, &s_can_termination, NULL,
    param_on_write_can_termination, 0.0f, 1.0f, true },

  /*
   * Board/clock diagnostics, read-only (see docs/bringup.md).  These are the
   * two questions every bring-up session starts with: did the watchdog reset
   * me, and is the 8 MHz oscillator actually being used?
   */
  { "board.reset_cause", NULL, PARAM_KIND_U32, 0u, &g_board_reset_cause, NULL, NULL,
    0.0f, PARAM_UNBOUNDED, true },
  { "board.clock_degraded", NULL, PARAM_KIND_U32, 0u, &g_board_clock_degraded, NULL, NULL,
    0.0f, 1.0f, true },
  { "board.core_clock", NULL, PARAM_KIND_U32, 0u, &system_core_clock, NULL, NULL,
    0.0f, PARAM_UNBOUNDED, true },

  /* USB CDC virtual COM port (see docs/bringup.md).  connected = 1 once the
   * host has enumerated and configured the interface. */
  { "usb.connected", NULL, PARAM_KIND_U32, 0u, &g_usb_cdc_connected, NULL, NULL,
    0.0f, 1.0f, true },
  { "usb.rx_bytes", NULL, PARAM_KIND_U32, 0u, &g_usb_cdc_rx_bytes, NULL, NULL,
    0.0f, PARAM_UNBOUNDED, true },
  { "usb.tx_dropped", NULL, PARAM_KIND_U32, 0u, &g_usb_cdc_tx_dropped, NULL, NULL,
    0.0f, PARAM_UNBOUNDED, true },
};

#define PARAM_COUNT  (sizeof(s_params) / sizeof(s_params[0]))

static const param_entry_t *param_find(const char *path)
{
  if (path == NULL)
  {
    return NULL;
  }

  for (uint32_t i = 0u; i < (uint32_t)PARAM_COUNT; i++)
  {
    if ((strcmp(path, s_params[i].path) == 0) ||
        ((s_params[i].alias != NULL) && (strcmp(path, s_params[i].alias) == 0)))
    {
      return &s_params[i];
    }
  }

  return NULL;
}

bool param_is_save_path(const char *path)
{
  return (strcmp(path, "save_configuration") == 0) ||
         (strcmp(path, "save_configuration()") == 0) ||
         (strcmp(path, "axis0.save_configuration") == 0) ||
         (strcmp(path, "axis0.save_configuration()") == 0) ||
         (strcmp(path, "axis0.config.save_configuration") == 0) ||
         (strcmp(path, "axis0.config.save_configuration()") == 0);
}

bool param_save_configuration(void)
{
  /* Runtime calibration results are the authoritative ones at save time. */
  if (g_axis.encoder_offset_valid)
  {
    g_odrive_config.encoder.pos_offset = g_axis.encoder.pos_offset;
    g_odrive_config.encoder.pre_calibrated = 1u;
  }
  if (g_axis.calibration_ok)
  {
    g_odrive_config.motor.pre_calibrated = 1u;
  }

  return nvm_config_save(&g_odrive_config);
}

void param_print(const char *path)
{
  const param_entry_t *entry = param_find(path);

  /*
   * ODrive ASCII semantics: a successful read answers with the bare value and
   * CRLF, nothing else.  That is what host libraries parse (e.g. the Arduino
   * ODrive library does readString().toFloat()), so no path prefix is emitted.
   */
  if (entry == NULL)
  {
    uart_comm_printf("invalid property\r\n");
    return;
  }

  if (entry->read_ptr == NULL)
  {
    /* The property exists in the namespace but cannot be read back. */
    uart_comm_printf("not implemented\r\n");
    return;
  }

  switch (entry->kind)
  {
    case PARAM_KIND_F32:
      uart_comm_printf("%.*f\r\n", (int)entry->precision,
                       (double)param_load_f32(entry->read_ptr));
      break;

    case PARAM_KIND_I32:
      uart_comm_printf("%ld\r\n", (long)param_load_i32(entry->read_ptr));
      break;

    case PARAM_KIND_U32:
      uart_comm_printf("%lu\r\n", (unsigned long)param_load_u32(entry->read_ptr));
      break;

    case PARAM_KIND_U16:
      uart_comm_printf("%lu\r\n", (unsigned long)param_load_u16(entry->read_ptr));
      break;

    default:
      uart_comm_printf("not implemented\r\n");
      break;
  }
}

static param_result_t param_store(const param_entry_t *entry, float value)
{
  if ((entry->config_ptr == NULL) && (entry->on_write == NULL))
  {
    return PARAM_RESULT_READ_ONLY;
  }

  /* NaN would slip through both bound comparisons. */
  if (value != value)
  {
    return PARAM_RESULT_OUT_OF_RANGE;
  }

  if ((value < entry->min_value) || (value > entry->max_value))
  {
    if (!entry->clamp)
    {
      return PARAM_RESULT_OUT_OF_RANGE;
    }
    value = util_clampf(value, entry->min_value, entry->max_value);
  }

  if (entry->config_ptr != NULL)
  {
    switch (entry->kind)
    {
      case PARAM_KIND_F32:
        *(float *)entry->config_ptr = value;
        break;
      case PARAM_KIND_I32:
        *(int32_t *)entry->config_ptr = (int32_t)value;
        break;
      case PARAM_KIND_U32:
        *(uint32_t *)entry->config_ptr = (uint32_t)value;
        break;
      default:
        break;
    }
  }

  if (entry->on_write != NULL)
  {
    entry->on_write(entry, value);
  }

  if (entry->config_ptr != NULL)
  {
    /* Re-publish every configuration-derived value into the live axis state. */
    nvm_config_apply(&g_odrive_config);
  }

  return PARAM_RESULT_OK;
}

param_result_t param_write_value(const char *path, float value)
{
  const param_entry_t *entry = param_find(path);

  if (entry == NULL)
  {
    return PARAM_RESULT_UNKNOWN;
  }

  return param_store(entry, value);
}

param_result_t param_write(const char *path, const char *value_text)
{
  const param_entry_t *entry;

  if ((path == NULL) || (value_text == NULL))
  {
    return PARAM_RESULT_MISSING_VALUE;
  }

  entry = param_find(path);
  if (entry == NULL)
  {
    return PARAM_RESULT_UNKNOWN;
  }

  return param_store(entry, strtof(value_text, NULL));
}
