#include "nvm_config.h"
#include "crc.h"
#include "system_time.h"

#include "axis.h"
#include "at32f435_437_flash.h"
#include "board.h"
#include "config.h"
#include "encoder_pll.h"

#include <stddef.h>
#include <string.h>

/*
 * Flash configuration layout:
 *   0x080FE000 .. 0x080FFFFF : reserved configuration area (8 KiB)
 *   0x080FE000              : single erased sector holding odrive_config_t
 *
 * The linker script only linked the application up to a low address (less
 * than 0x0800A000 in the current image), so this area is far outside the
 * firmware image.  Keep this address sector-aligned.
 */
typedef char nvm_config_assert_size[(sizeof(odrive_config_t) % 4u == 0u) ? 1 : -1];
typedef char nvm_config_assert_motor_size[(sizeof(odrive_motor_config_t) == 44u) ? 1 : -1];
typedef char nvm_config_assert_encoder_size[(sizeof(odrive_encoder_config_t) == 20u) ? 1 : -1];
typedef char nvm_config_assert_controller_size[(sizeof(odrive_controller_config_t) == 72u) ? 1 : -1];
typedef char nvm_config_assert_axis_size[(sizeof(odrive_axis_config_t) == 20u) ? 1 : -1];
typedef char nvm_config_assert_comm_size[(sizeof(odrive_comm_config_t) == 16u) ? 1 : -1];
typedef char nvm_config_assert_motor_offset[(offsetof(odrive_config_t, motor) == 12u) ? 1 : -1];
typedef char nvm_config_assert_encoder_offset[(offsetof(odrive_config_t, encoder) == 56u) ? 1 : -1];
typedef char nvm_config_assert_controller_offset[(offsetof(odrive_config_t, controller) == 76u) ? 1 : -1];
typedef char nvm_config_assert_axis_offset[(offsetof(odrive_config_t, axis) == 148u) ? 1 : -1];
typedef char nvm_config_assert_comm_offset[(offsetof(odrive_config_t, comm) == 168u) ? 1 : -1];
typedef char nvm_config_assert_total_size[(sizeof(odrive_config_t) == 184u) ? 1 : -1];

odrive_config_t g_odrive_config;

static float nvm_config_positive_or(float value, float fallback)
{
  return (value > 0.0f) ? value : fallback;
}

static float nvm_config_nonnegative(float value)
{
  return (value > 0.0f) ? value : 0.0f;
}

void nvm_config_defaults(odrive_config_t *cfg)
{
  if (cfg == NULL)
  {
    return;
  }

  memset(cfg, 0, sizeof(*cfg));

  cfg->magic = NVM_CONFIG_MAGIC;
  cfg->version = NVM_CONFIG_VERSION;
  cfg->crc = 0u;

  cfg->motor.pole_pairs = (int32_t)MOTOR_POLE_PAIRS;
  cfg->motor.phase_resistance = MOTOR_PHASE_RESISTANCE_OHM;
  cfg->motor.phase_inductance = MOTOR_PHASE_INDUCTANCE_H;
  cfg->motor.current_lim = MOTOR_CURRENT_LIMIT_A;
  cfg->motor.current_lim_margin = MOTOR_CURRENT_LIMIT_MARGIN_A;
  cfg->motor.torque_constant = 0.04f;
  cfg->motor.calibration_current = 2.0f;
  cfg->motor.resistance_calib_max_voltage = 2.0f;
  cfg->motor.pre_calibrated = 0u;
  cfg->motor.r_wl_ff_enable = 1u;
  cfg->motor.bemf_ff_enable = 1u;

  cfg->encoder.cpr = ENCODER_MT6816_CPR;
  cfg->encoder.direction = ENCODER_DIRECTION_DEFAULT;
  cfg->encoder.bandwidth = ENCODER_PLL_DEFAULT_BANDWIDTH_HZ;
  cfg->encoder.pos_offset = 0.0f;
  cfg->encoder.pre_calibrated = 0u;

  cfg->controller.control_mode = (int32_t)CONTROL_MODE_POSITION_CONTROL;
  cfg->controller.input_mode = (int32_t)INPUT_MODE_PASSTHROUGH;
  cfg->controller.pos_gain = 20.0f;
  cfg->controller.vel_gain = 1.0f / 6.0f;
  cfg->controller.vel_integrator_gain = 2.0f / 6.0f;
  cfg->controller.vel_integrator_limit = 12.0f;
  cfg->controller.vel_limit = 2.0f;
  cfg->controller.vel_limit_tolerance = 1.2f;
  cfg->controller.current_limit = MOTOR_CURRENT_LIMIT_A;
  cfg->controller.torque_limit = 12.0f;
  cfg->controller.vel_ramp_rate = 1.0f;
  cfg->controller.torque_ramp_rate = 0.1f;
  cfg->controller.pos_filter_bandwidth = 2.0f;
  cfg->controller.traj_vel_limit = CONTROLLER_VEL_LIMIT_REV_S;
  cfg->controller.traj_accel_limit = 5.0f;
  cfg->controller.traj_decel_limit = 5.0f;
  cfg->controller.circular_setpoints = 0u;
  cfg->controller.circular_setpoint_range = 1.0f;

  cfg->axis.startup_motor_calibration = 0u;
  cfg->axis.startup_encoder_offset_calibration = 0u;
  cfg->axis.startup_closed_loop_control = 0u;
  cfg->axis.enable_watchdog = 0u;
  cfg->axis.watchdog_timeout = 200u;

  cfg->comm.can_node_id = CAN_NODE_ID_DEFAULT;
  cfg->comm.can_heartbeat_rate_ms = 100u;
  cfg->comm.uart_baudrate = UART_BAUDRATE_DEFAULT;
  cfg->comm.can_baudrate = CAN_BAUDRATE_DEFAULT;
}

uint32_t nvm_config_crc(const odrive_config_t *cfg)
{
  uint32_t crc = CRC32_INITIAL_VALUE;
  uint32_t crc_offset;
  const uint8_t *bytes;

  if (cfg == NULL)
  {
    return 0u;
  }

  bytes = (const uint8_t *)cfg;
  crc_offset = (uint32_t)offsetof(odrive_config_t, crc);

  for (uint32_t i = 0u; i < (uint32_t)sizeof(*cfg); i++)
  {
    uint8_t data = bytes[i];

    /* The CRC field itself is treated as zero so save and load agree. */
    if ((i >= crc_offset) && (i < (crc_offset + 4u)))
    {
      data = 0u;
    }

    crc = crc32_update_byte(crc, data);
  }

  return crc ^ CRC32_INITIAL_VALUE;
}

bool nvm_config_load(odrive_config_t *cfg)
{
  odrive_config_t image;

  if (cfg == NULL)
  {
    return false;
  }

  memcpy(&image, (const void *)NVM_CONFIG_FLASH_ADDR, sizeof(image));

  if (image.magic != NVM_CONFIG_MAGIC)
  {
    return false;
  }

  if (image.version != NVM_CONFIG_VERSION)
  {
    return false;
  }

  if (image.crc != nvm_config_crc(&image))
  {
    return false;
  }

  *cfg = image;
  return true;
}

bool nvm_config_save(const odrive_config_t *cfg)
{
  odrive_config_t image;
  flash_status_type status;
  bool ok = true;
  uint32_t offset;

  if (cfg == NULL)
  {
    return false;
  }

  image = *cfg;
  image.magic = NVM_CONFIG_MAGIC;
  image.version = NVM_CONFIG_VERSION;
  image.crc = 0u;
  image.crc = nvm_config_crc(&image);

  flash_unlock();

  status = flash_sector_erase(NVM_CONFIG_FLASH_ADDR);
  if (status != FLASH_OPERATE_DONE)
  {
    ok = false;
  }

  for (offset = 0u; ok && (offset < (uint32_t)sizeof(image)); offset += 4u)
  {
    uint32_t word;

    memcpy(&word, ((const uint8_t *)&image) + offset, sizeof(word));
    status = flash_word_program(NVM_CONFIG_FLASH_ADDR + offset, word);
    if (status != FLASH_OPERATE_DONE)
    {
      ok = false;
    }
  }

  for (offset = 0u; ok && (offset < (uint32_t)sizeof(image)); offset += 4u)
  {
    uint32_t expected;
    uint32_t actual;

    memcpy(&expected, ((const uint8_t *)&image) + offset, sizeof(expected));
    actual = *(const volatile uint32_t *)(NVM_CONFIG_FLASH_ADDR + offset);
    if (actual != expected)
    {
      ok = false;
    }
  }

  flash_lock();
  return ok;
}

void nvm_config_apply(odrive_config_t *cfg)
{
  controller_t *ctrl;
  float cpr;
  float bandwidth;
  int32_t pole_pairs;

  if (cfg == NULL)
  {
    return;
  }

  /*
   * This function is the only place that publishes configuration into the live
   * axis state, and it is called after every parameter write as well as at
   * boot.  It must therefore be idempotent and must never destroy a calibration
   * that was performed at runtime but not saved yet:
   *   - the electrical offset is only restored from the stored value when a
   *     calibration was actually saved (pre_calibrated != 0);
   *   - calibration validity is only ever raised, never cleared.
   */
  cpr = nvm_config_positive_or(cfg->encoder.cpr, ENCODER_MT6816_CPR);
  bandwidth = nvm_config_positive_or(cfg->encoder.bandwidth, ENCODER_PLL_DEFAULT_BANDWIDTH_HZ);
  pole_pairs = (cfg->motor.pole_pairs > 0) ? cfg->motor.pole_pairs : (int32_t)MOTOR_POLE_PAIRS;

  g_axis.encoder.pole_pairs = pole_pairs;
  g_axis.phase_current_limit = nvm_config_nonnegative(cfg->motor.current_lim);
  g_axis.phase_resistance = nvm_config_positive_or(cfg->motor.phase_resistance, MOTOR_PHASE_RESISTANCE_OHM);
  g_axis.phase_inductance = nvm_config_positive_or(cfg->motor.phase_inductance, MOTOR_PHASE_INDUCTANCE_H);
  g_axis.motor_torque_constant = nvm_config_positive_or(cfg->motor.torque_constant, 0.04f);
  g_axis.motor_flux_linkage = g_axis.motor_torque_constant / (1.5f * (float)pole_pairs);
  g_axis.r_wl_ff_enable = (cfg->motor.r_wl_ff_enable != 0u);
  g_axis.bemf_ff_enable = (cfg->motor.bemf_ff_enable != 0u);

  g_axis.encoder.cpr = cpr;
  g_axis.encoder.direction = (cfg->encoder.direction >= 0.0f) ? 1.0f : -1.0f;
  encoder_set_bandwidth(&g_axis.encoder, bandwidth);
  if (cfg->encoder.pre_calibrated != 0u)
  {
    encoder_set_offset(&g_axis.encoder, cfg->encoder.pos_offset);
  }

  ctrl = &g_axis.controller;
  controller_set_control_mode(ctrl,
                              (controller_mode_t)cfg->controller.control_mode,
                              (input_mode_t)cfg->controller.input_mode);
  ctrl->pos_gain = cfg->controller.pos_gain;
  ctrl->vel_gain = cfg->controller.vel_gain;
  ctrl->vel_integrator_gain = cfg->controller.vel_integrator_gain;
  ctrl->vel_integrator_limit = nvm_config_nonnegative(cfg->controller.vel_integrator_limit);
  ctrl->vel_limit = nvm_config_nonnegative(cfg->controller.vel_limit);
  ctrl->vel_limit_tolerance = nvm_config_nonnegative(cfg->controller.vel_limit_tolerance);
  ctrl->torque_limit = nvm_config_nonnegative(cfg->controller.torque_limit);
  ctrl->vel_ramp_rate = nvm_config_nonnegative(cfg->controller.vel_ramp_rate);
  ctrl->torque_ramp_rate = nvm_config_nonnegative(cfg->controller.torque_ramp_rate);
  ctrl->pos_filter_bandwidth = nvm_config_positive_or(cfg->controller.pos_filter_bandwidth, 2.0f);
  ctrl->circular_setpoints = (cfg->controller.circular_setpoints != 0u);
  ctrl->circular_setpoint_range = nvm_config_positive_or(cfg->controller.circular_setpoint_range, 1.0f);
  ctrl->traj_vel_limit = nvm_config_positive_or(cfg->controller.traj_vel_limit, CONTROLLER_VEL_LIMIT_REV_S);
  ctrl->traj_accel_limit = nvm_config_positive_or(cfg->controller.traj_accel_limit, 1.0f);
  ctrl->traj_decel_limit = nvm_config_positive_or(cfg->controller.traj_decel_limit, 1.0f);
  ctrl->traj.config.vel_limit = ctrl->traj_vel_limit;
  ctrl->traj.config.accel_limit = ctrl->traj_accel_limit;
  ctrl->traj.config.decel_limit = ctrl->traj_decel_limit;
  controller_set_current_limits(ctrl, nvm_config_nonnegative(cfg->controller.current_limit));

  g_axis.communication_watchdog_enabled = (cfg->axis.enable_watchdog != 0u);
  g_axis.communication_watchdog_timeout_ms = cfg->axis.watchdog_timeout;
  g_axis.last_communication_ms = system_millis();
  g_axis.can_node_id = cfg->comm.can_node_id;

  /* A closed loop needs both the motor and encoder calibration records.  These
   * flags are only raised here; a saved calibration is never revoked by an
   * ordinary parameter write. */
  if (cfg->motor.pre_calibrated != 0u)
  {
    g_axis.calibration_ok = true;
    g_axis.motor_calibrated = true;
  }
  if (cfg->encoder.pre_calibrated != 0u)
  {
    g_axis.encoder_offset_valid = true;
  }
}
