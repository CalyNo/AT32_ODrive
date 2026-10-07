#ifndef AT32_ODRIVE_NVM_CONFIG_H
#define AT32_ODRIVE_NVM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Persistent ODrive-style configuration for the single-axis MiniOdrive.
 *
 * The on-flash image is the raw odrive_config_t.  All fields are 4-byte
 * scalars and the structure therefore has no compiler-dependent bitfields
 * or packed padding.  Change the layout only with a version bump.
 */
#define NVM_CONFIG_MAGIC                 ((uint32_t)0x4F445256u) /* "ODRV" */
#define NVM_CONFIG_VERSION               ((uint32_t)0x00000003u)

/* Reserved the final 8 KiB of the 1 MiB internal flash. */
#define NVM_CONFIG_FLASH_ADDR            ((uint32_t)0x080FE000u)
#define NVM_CONFIG_FLASH_SIZE            ((uint32_t)0x00002000u)

typedef struct
{
  int32_t pole_pairs;
  float phase_resistance;
  float phase_inductance;
  float current_lim;
  float current_lim_margin;
  float torque_constant;
  float calibration_current;
  float resistance_calib_max_voltage;
  uint32_t pre_calibrated;
  uint32_t r_wl_ff_enable;
  uint32_t bemf_ff_enable;
} odrive_motor_config_t;

typedef struct
{
  float cpr;
  float direction;
  float bandwidth;
  float pos_offset;
  uint32_t pre_calibrated;
} odrive_encoder_config_t;

typedef struct
{
  int32_t control_mode;
  int32_t input_mode;
  float pos_gain;
  float vel_gain;
  float vel_integrator_gain;
  float vel_integrator_limit;
  float vel_limit;
  float vel_limit_tolerance;
  float current_limit;
  float torque_limit;
  float vel_ramp_rate;
  float torque_ramp_rate;
  float pos_filter_bandwidth;
  float traj_vel_limit;
  float traj_accel_limit;
  float traj_decel_limit;
  uint32_t circular_setpoints;
  float circular_setpoint_range;
} odrive_controller_config_t;

typedef struct
{
  uint32_t startup_motor_calibration;
  uint32_t startup_encoder_offset_calibration;
  uint32_t startup_closed_loop_control;
  uint32_t enable_watchdog;
  uint32_t watchdog_timeout; /* milliseconds */
} odrive_axis_config_t;

typedef struct
{
  uint32_t can_node_id;
  uint32_t can_heartbeat_rate_ms;
  uint32_t uart_baudrate;
  uint32_t can_baudrate;         /* applied on CAN re-init / reboot */
} odrive_comm_config_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t crc;

  odrive_motor_config_t motor;
  odrive_encoder_config_t encoder;
  odrive_controller_config_t controller;
  odrive_axis_config_t axis;
  odrive_comm_config_t comm;
} odrive_config_t;

extern odrive_config_t g_odrive_config;

void nvm_config_defaults(odrive_config_t *cfg);
bool nvm_config_load(odrive_config_t *cfg);
bool nvm_config_save(const odrive_config_t *cfg);
uint32_t nvm_config_crc(const odrive_config_t *cfg);
void nvm_config_apply(odrive_config_t *cfg);

#endif /* AT32_ODRIVE_NVM_CONFIG_H */
