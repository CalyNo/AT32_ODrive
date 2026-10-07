#include "controller.h"
#include "config.h"
#include "util.h"

#include <math.h>
#include <stddef.h>

void controller_init(controller_t *ctrl)
{
  traptraj_config_t traj_config;

  if (ctrl == NULL)
  {
    return;
  }

  ctrl->control_mode = CONTROL_MODE_POSITION_CONTROL;
  ctrl->input_mode = INPUT_MODE_PASSTHROUGH;

  ctrl->pos_input = 0.0f;
  ctrl->vel_input = 0.0f;
  ctrl->torque_input = 0.0f;

  ctrl->pos_setpoint = 0.0f;
  ctrl->vel_setpoint = 0.0f;
  ctrl->torque_setpoint = 0.0f;

  /*
   * Tuning values are persisted in nvm_config, so their defaults live in
   * nvm_config_defaults() only.  They are zeroed here and published by
   * nvm_config_apply(), which runs before any interrupt can use them.
   */
  ctrl->pos_gain = 0.0f;
  ctrl->vel_gain = 0.0f;
  ctrl->vel_integrator_gain = 0.0f;
  ctrl->vel_integrator_limit = 0.0f;

  ctrl->vel_limit = 0.0f;
  ctrl->vel_limit_tolerance = 0.0f;
  ctrl->current_limit = 0.0f;
  ctrl->torque_limit = 0.0f;

  ctrl->vel_ramp_rate = 0.0f;
  ctrl->torque_ramp_rate = 0.0f;
  ctrl->pos_filter_bandwidth = 0.0f;
  ctrl->inertia = 0.0f;
  ctrl->circular_setpoints = false;
  ctrl->circular_setpoint_range = 0.0f;
  ctrl->pos_feedforward = 0.0f;
  ctrl->vel_feedforward = 0.0f;
  ctrl->torque_feedforward = 0.0f;

  ctrl->i_d_setpoint = 0.0f;
  ctrl->i_q_setpoint = 0.0f;
  ctrl->v_d_setpoint = 0.0f;
  ctrl->v_q_setpoint = 0.0f;

  ctrl->pos_error = 0.0f;
  ctrl->vel_error = 0.0f;
  ctrl->trajectory_done = true;

  ctrl->traj_t = 0.0f;
  ctrl->pos_input_updated = false;
  ctrl->traj_vel_limit = 0.0f;
  ctrl->traj_accel_limit = 0.0f;
  ctrl->traj_decel_limit = 0.0f;

  traj_config.vel_limit = ctrl->traj_vel_limit;
  traj_config.accel_limit = ctrl->traj_accel_limit;
  traj_config.decel_limit = ctrl->traj_decel_limit;
  traptraj_init(&ctrl->traj, &traj_config);

  /* Current loop gains are not part of the persisted configuration. */
  pid_init(&ctrl->current_d_pid, 0.5f, 200.0f, 0.0f, -12.0f, 12.0f, -12.0f, 12.0f);
  pid_init(&ctrl->current_q_pid, 0.5f, 200.0f, 0.0f, -12.0f, 12.0f, -12.0f, 12.0f);
}

void controller_reset(controller_t *ctrl)
{
  if (ctrl == NULL)
  {
    return;
  }

  pid_reset(&ctrl->current_d_pid);
  pid_reset(&ctrl->current_q_pid);
  ctrl->i_d_setpoint = 0.0f;
  ctrl->i_q_setpoint = 0.0f;
  ctrl->v_d_setpoint = 0.0f;
  ctrl->v_q_setpoint = 0.0f;
  ctrl->pos_error = 0.0f;
  ctrl->vel_error = 0.0f;
  ctrl->trajectory_done = true;
  ctrl->traj_t = 0.0f;
  ctrl->pos_input_updated = true;
}

void controller_set_control_mode(controller_t *ctrl, controller_mode_t mode, input_mode_t input_mode)
{
  if (ctrl == NULL)
  {
    return;
  }
  ctrl->control_mode = mode;
  ctrl->input_mode = input_mode;
}

void controller_set_input_pos(controller_t *ctrl, float pos)
{
  if (ctrl != NULL)
  {
    ctrl->pos_input = pos;
    ctrl->pos_input_updated = true;
  }
}

void controller_set_input_vel(controller_t *ctrl, float vel)
{
  if (ctrl != NULL)
  {
    ctrl->vel_input = vel;
  }
}

void controller_set_input_torque(controller_t *ctrl, float torque)
{
  if (ctrl != NULL)
  {
    ctrl->torque_input = torque;
  }
}

void controller_set_current_limits(controller_t *ctrl, float limit_a)
{
  if (ctrl == NULL)
  {
    return;
  }

  if (limit_a < 0.0f)
  {
    limit_a = 0.0f;
  }
  ctrl->current_limit = limit_a;
}

static void controller_update_input_mode(controller_t *ctrl, float pos_estimate, float vel_estimate, float dt)
{
  switch (ctrl->input_mode)
  {
    case INPUT_MODE_INACTIVE:
      /* Keep last setpoints. */
      break;

    case INPUT_MODE_VEL_RAMP:
      if (ctrl->vel_setpoint < ctrl->vel_input)
      {
        ctrl->vel_setpoint += ctrl->vel_ramp_rate * dt;
        if (ctrl->vel_setpoint > ctrl->vel_input)
        {
          ctrl->vel_setpoint = ctrl->vel_input;
        }
      }
      else if (ctrl->vel_setpoint > ctrl->vel_input)
      {
        ctrl->vel_setpoint -= ctrl->vel_ramp_rate * dt;
        if (ctrl->vel_setpoint < ctrl->vel_input)
        {
          ctrl->vel_setpoint = ctrl->vel_input;
        }
      }
      ctrl->pos_setpoint = pos_estimate;
      ctrl->torque_setpoint = ctrl->torque_input;
      break;

    case INPUT_MODE_POS_FILTER:
    {
      /* Critically damped second-order position tracking filter. */
      float omega = 2.0f * UTIL_PI_F * ctrl->pos_filter_bandwidth;
      float pos_error = ctrl->pos_input - ctrl->pos_setpoint;
      float accel = omega * omega * pos_error - 2.0f * omega * ctrl->vel_setpoint;

      if (ctrl->circular_setpoints)
      {
        float half_range = 0.5f * ctrl->circular_setpoint_range;
        pos_error = util_wrap_range(ctrl->pos_input - ctrl->pos_setpoint + half_range,
                                  ctrl->circular_setpoint_range) - half_range;
        accel = omega * omega * pos_error - 2.0f * omega * ctrl->vel_setpoint;
      }

      ctrl->vel_setpoint += accel * dt;
      ctrl->pos_setpoint += ctrl->vel_setpoint * dt;
      ctrl->torque_setpoint = ctrl->torque_input;
      break;
    }

    case INPUT_MODE_TRAP_TRAJ:
    {
      traptraj_step_t step;

      if (ctrl->pos_input_updated)
      {
        ctrl->traj_vel_limit = (ctrl->traj_vel_limit > 0.0f) ? ctrl->traj_vel_limit : fabsf(ctrl->vel_limit);
        ctrl->traj_accel_limit = (ctrl->traj_accel_limit > 0.0f) ? ctrl->traj_accel_limit : 1.0f;
        ctrl->traj_decel_limit = (ctrl->traj_decel_limit > 0.0f) ? ctrl->traj_decel_limit : 1.0f;
        ctrl->traj.config.vel_limit = ctrl->traj_vel_limit;
        ctrl->traj.config.accel_limit = ctrl->traj_accel_limit;
        ctrl->traj.config.decel_limit = ctrl->traj_decel_limit;
        (void)traptraj_plan(&ctrl->traj, ctrl->pos_input, pos_estimate, vel_estimate,
                            ctrl->traj_vel_limit, ctrl->traj_accel_limit, ctrl->traj_decel_limit);
        ctrl->traj_t = 0.0f;
        ctrl->pos_input_updated = false;
      }

      step = traptraj_eval(&ctrl->traj, ctrl->traj_t);
      ctrl->pos_setpoint = ctrl->circular_setpoints
                               ? util_wrap_range(step.y, ctrl->circular_setpoint_range)
                               : step.y;
      ctrl->vel_setpoint = step.yd;
      ctrl->trajectory_done = traptraj_done(&ctrl->traj, ctrl->traj_t);
      ctrl->traj_t += dt;
      ctrl->torque_setpoint = ctrl->torque_input;
      break;
    }

    case INPUT_MODE_TORQUE_RAMP:
      if (ctrl->torque_setpoint < ctrl->torque_input)
      {
        ctrl->torque_setpoint += ctrl->torque_ramp_rate * dt;
        if (ctrl->torque_setpoint > ctrl->torque_input)
        {
          ctrl->torque_setpoint = ctrl->torque_input;
        }
      }
      else if (ctrl->torque_setpoint > ctrl->torque_input)
      {
        ctrl->torque_setpoint -= ctrl->torque_ramp_rate * dt;
        if (ctrl->torque_setpoint < ctrl->torque_input)
        {
          ctrl->torque_setpoint = ctrl->torque_input;
        }
      }
      ctrl->pos_setpoint = pos_estimate;
      ctrl->vel_setpoint = vel_estimate;
      break;

    case INPUT_MODE_PASSTHROUGH:
    case INPUT_MODE_MIX_CHANNELS:
    case INPUT_MODE_MIRROR:
    case INPUT_MODE_TUNING:
    default:
      ctrl->pos_setpoint = ctrl->pos_input;
      ctrl->vel_setpoint = ctrl->vel_input;
      ctrl->torque_setpoint = ctrl->torque_input;
      break;
  }
}

static void controller_run_velocity_loop(controller_t *ctrl, float vel_estimate, float dt, float torque_ff)
{
  float vel_error = ctrl->vel_setpoint - vel_estimate;
  float torque_cmd;

  ctrl->vel_error = vel_error;
  torque_cmd = ctrl->vel_gain * vel_error;
  torque_cmd += util_clampf(ctrl->vel_integrator_gain * vel_error * dt,
                       -ctrl->vel_integrator_limit,
                       ctrl->vel_integrator_limit);
  torque_cmd += torque_ff;
  ctrl->i_q_setpoint = util_clampf(torque_cmd, -ctrl->current_limit, ctrl->current_limit);
  ctrl->i_d_setpoint = 0.0f;
}

static void controller_limit_current_vector(controller_t *ctrl)
{
  float magnitude;

  if (ctrl == NULL)
  {
    return;
  }

  magnitude = sqrtf((ctrl->i_d_setpoint * ctrl->i_d_setpoint) +
                    (ctrl->i_q_setpoint * ctrl->i_q_setpoint));
  if ((magnitude > ctrl->current_limit) && (magnitude > 1.0e-6f))
  {
    float scale = ctrl->current_limit / magnitude;
    ctrl->i_d_setpoint *= scale;
    ctrl->i_q_setpoint *= scale;
  }
}

void controller_update(controller_t *ctrl, float pos_estimate, float vel_estimate, float dt)
{
  float vel_cmd;
  float pos_error;
  float torque_ff;

  if ((ctrl == NULL) || (dt <= 0.0f))
  {
    return;
  }

  controller_update_input_mode(ctrl, pos_estimate, vel_estimate, dt);
  torque_ff = ctrl->torque_setpoint + ctrl->torque_feedforward;

  switch (ctrl->control_mode)
  {
    case CONTROL_MODE_POSITION_CONTROL:
      pos_error = ctrl->pos_setpoint - pos_estimate;
      if (ctrl->circular_setpoints)
      {
        float half_range = 0.5f * ctrl->circular_setpoint_range;
        pos_error = util_wrap_range(ctrl->pos_input - pos_estimate + half_range,
                                  ctrl->circular_setpoint_range) - half_range;
      }
      ctrl->pos_error = pos_error;
      vel_cmd = ctrl->pos_gain * pos_error + ctrl->vel_setpoint + ctrl->vel_feedforward;
      ctrl->vel_setpoint = util_clampf(vel_cmd, -ctrl->vel_limit, ctrl->vel_limit);
      controller_run_velocity_loop(ctrl, vel_estimate, dt, torque_ff);
      break;

    case CONTROL_MODE_VELOCITY_CONTROL:
      controller_run_velocity_loop(ctrl, vel_estimate, dt, torque_ff);
      break;

    case CONTROL_MODE_TORQUE_CONTROL:
    default:
      ctrl->i_q_setpoint = util_clampf(torque_ff, -ctrl->current_limit, ctrl->current_limit);
      ctrl->i_d_setpoint = 0.0f;
      break;
  }

  controller_limit_current_vector(ctrl);
}

void controller_update_current_references(controller_t *ctrl, float i_d_measured, float i_q_measured, float dt)
{
  float v_d;
  float v_q;

  if ((ctrl == NULL) || (dt <= 0.0f))
  {
    return;
  }

  v_d = pid_update(&ctrl->current_d_pid, ctrl->i_d_setpoint - i_d_measured, dt);
  v_q = pid_update(&ctrl->current_q_pid, ctrl->i_q_setpoint - i_q_measured, dt);
  ctrl->v_d_setpoint = v_d;
  ctrl->v_q_setpoint = v_q;
}
