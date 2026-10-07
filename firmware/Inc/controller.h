#ifndef AT32_ODRIVE_CONTROLLER_H
#define AT32_ODRIVE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "pid.h"
#include "traptraj.h"

/*
 * Values follow ODrive's ODrive.Controller.ControlMode / InputMode order.
 * The shorter historical aliases are kept for the existing code.
 */
typedef enum
{
  CONTROL_MODE_VOLTAGE_CONTROL = 0,
  CONTROL_MODE_TORQUE_CONTROL = 1,
  CONTROL_MODE_TORQUE = 1,
  CONTROL_MODE_VELOCITY_CONTROL = 2,
  CONTROL_MODE_VELOCITY = 2,
  CONTROL_MODE_POSITION_CONTROL = 3,
  CONTROL_MODE_POSITION = 3
} controller_mode_t;

typedef enum
{
  INPUT_MODE_INACTIVE = 0,
  INPUT_MODE_PASSTHROUGH = 1,
  INPUT_MODE_VEL_RAMP = 2,
  INPUT_MODE_POS_FILTER = 3,
  INPUT_MODE_MIX_CHANNELS = 4,
  INPUT_MODE_TRAP_TRAJ = 5,
  INPUT_MODE_TORQUE_RAMP = 6,
  INPUT_MODE_MIRROR = 7,
  INPUT_MODE_TUNING = 8
} input_mode_t;

typedef struct
{
  controller_mode_t control_mode;
  input_mode_t input_mode;

  /* User inputs. */
  float pos_input;
  float vel_input;
  float torque_input;

  /* Reference values after input mode processing. */
  float pos_setpoint;
  float vel_setpoint;
  float torque_setpoint;

  /* Gains. */
  float pos_gain;
  float vel_gain;
  float vel_integrator_gain;
  float vel_integrator_limit;

  /* Limits. */
  float vel_limit;
  float vel_limit_tolerance;
  float current_limit;
  float torque_limit;

  /* Input shaping. */
  float vel_ramp_rate;
  float torque_ramp_rate;
  float pos_filter_bandwidth;
  float inertia;
  bool circular_setpoints;
  float circular_setpoint_range;
  float pos_feedforward;
  float vel_feedforward;
  float torque_feedforward;

  /* Current references and voltage outputs. */
  float i_d_setpoint;
  float i_q_setpoint;
  float v_d_setpoint;
  float v_q_setpoint;

  pid_t current_d_pid;
  pid_t current_q_pid;

  /* Debug / status. */
  float pos_error;
  float vel_error;
  bool trajectory_done;

  /* Trapezoidal trajectory planner. */
  traptraj_t traj;
  float traj_t;
  bool pos_input_updated;
  float traj_vel_limit;
  float traj_accel_limit;
  float traj_decel_limit;
} controller_t;

void controller_init(controller_t *ctrl);
void controller_reset(controller_t *ctrl);
void controller_set_control_mode(controller_t *ctrl, controller_mode_t mode, input_mode_t input_mode);
void controller_set_input_pos(controller_t *ctrl, float pos);
void controller_set_input_vel(controller_t *ctrl, float vel);
void controller_set_input_torque(controller_t *ctrl, float torque);
void controller_update(controller_t *ctrl, float pos_estimate, float vel_estimate, float dt);
void controller_update_current_references(controller_t *ctrl, float i_d_measured, float i_q_measured, float dt);
void controller_set_current_limits(controller_t *ctrl, float limit_a);

#endif /* AT32_ODRIVE_CONTROLLER_H */
