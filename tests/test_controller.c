/*
 * Host-side tests for controller.c.
 *
 * controller.c is free of target dependencies, so the control law, the input
 * shaping modes and the setpoint limiting can be verified on the host build.
 *
 * Note: controller_init() intentionally leaves every persisted tuning value at
 * zero (nvm_config_defaults()/nvm_config_apply() own those defaults), so each
 * case below sets the gains and limits it depends on explicitly.
 */
#include "controller.h"

#include <math.h>
#include <stdio.h>

static int g_failures;

static void check(const char *name, int ok)
{
  if (!ok)
  {
    printf("FAIL %s\n", name);
    g_failures++;
  }
}

static int nearf(float actual, float expected, float tol)
{
  return fabsf(actual - expected) <= tol;
}

static void check_near(const char *name, float actual, float expected, float tol)
{
  if (!nearf(actual, expected, tol))
  {
    printf("FAIL %s: actual=%.6f expected=%.6f tol=%.6f\n", name, actual, expected, tol);
    g_failures++;
  }
}

/* Torque mode: input is the q-axis current request, clamped to the limit. */
static void test_torque_mode(void)
{
  controller_t ctrl;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 5.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_TORQUE_CONTROL, INPUT_MODE_PASSTHROUGH);

  controller_set_input_torque(&ctrl, 3.0f);
  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("torque.in_range.iq", ctrl.i_q_setpoint, 3.0f, 1e-4f);
  check_near("torque.in_range.id", ctrl.i_d_setpoint, 0.0f, 1e-6f);

  controller_set_input_torque(&ctrl, 9.0f);
  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("torque.clamped.iq", ctrl.i_q_setpoint, 5.0f, 1e-4f);

  controller_set_input_torque(&ctrl, -9.0f);
  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("torque.clamped.neg_iq", ctrl.i_q_setpoint, -5.0f, 1e-4f);
}

/* Velocity mode: proportional term must follow the velocity error. */
static void test_velocity_mode(void)
{
  controller_t ctrl;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_PASSTHROUGH);

  ctrl.vel_gain = 1.0f;
  ctrl.vel_integrator_gain = 0.0f;
  controller_set_input_vel(&ctrl, 1.0f);

  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("velocity.error", ctrl.vel_error, 1.0f, 1e-4f);
  check_near("velocity.iq", ctrl.i_q_setpoint, 1.0f, 1e-4f);

  /* Integrator must accumulate ki * error * dt when enabled. */
  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_PASSTHROUGH);
  ctrl.vel_gain = 0.0f;
  ctrl.vel_integrator_gain = 2.0f;
  ctrl.vel_integrator_limit = 10.0f;
  controller_set_input_vel(&ctrl, 1.0f);

  controller_update(&ctrl, 0.0f, 0.0f, 0.1f);
  check_near("velocity.integrator.iq", ctrl.i_q_setpoint, 0.2f, 1e-4f);
}

/* Velocity mode honours the current limit. */
static void test_velocity_current_limit(void)
{
  controller_t ctrl;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 2.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_PASSTHROUGH);
  ctrl.vel_gain = 10.0f;
  ctrl.vel_integrator_gain = 0.0f;
  controller_set_input_vel(&ctrl, 5.0f);

  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("velocity.current_limit.iq", ctrl.i_q_setpoint, 2.0f, 1e-4f);
}

/* Position mode: the velocity command derived from the position error is
 * limited by vel_limit before the velocity loop runs. */
static void test_position_mode(void)
{
  controller_t ctrl;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_POSITION_CONTROL, INPUT_MODE_PASSTHROUGH);
  ctrl.pos_gain = 10.0f;
  ctrl.vel_limit = 0.5f;
  ctrl.vel_gain = 1.0f;
  ctrl.vel_integrator_gain = 0.0f;
  controller_set_input_pos(&ctrl, 1.0f);

  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("position.error", ctrl.pos_error, 1.0f, 1e-4f);
  check_near("position.vel_limit", ctrl.vel_setpoint, 0.5f, 1e-4f);
  check_near("position.iq", ctrl.i_q_setpoint, 0.5f, 1e-4f);

  /* Inside the limit the gain is not clipped. */
  ctrl.vel_setpoint = 0.0f;
  controller_set_input_pos(&ctrl, 0.02f);
  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("position.unclipped.vel", ctrl.vel_setpoint, 0.2f, 1e-4f);
}

/* INPUT_MODE_VEL_RAMP: ramp rate limits the setpoint slew. */
static void test_vel_ramp(void)
{
  controller_t ctrl;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_VEL_RAMP);
  ctrl.vel_ramp_rate = 2.0f;
  controller_set_input_vel(&ctrl, 1.0f);

  controller_update(&ctrl, 0.0f, 0.0f, 0.1f);
  check_near("vel_ramp.step1", ctrl.vel_setpoint, 0.2f, 1e-4f);

  controller_update(&ctrl, 0.0f, 0.0f, 1.0f);
  check_near("vel_ramp.saturate", ctrl.vel_setpoint, 1.0f, 1e-4f);

  /* Ramp back down towards a lower target. */
  controller_set_input_vel(&ctrl, 0.0f);
  controller_update(&ctrl, 0.0f, 0.0f, 0.1f);
  check_near("vel_ramp.step_down", ctrl.vel_setpoint, 0.8f, 1e-4f);
}

/* INPUT_MODE_TORQUE_RAMP: ramp rate limits the torque request. */
static void test_torque_ramp(void)
{
  controller_t ctrl;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_TORQUE_CONTROL, INPUT_MODE_TORQUE_RAMP);
  ctrl.torque_ramp_rate = 1.0f;
  controller_set_input_torque(&ctrl, 2.0f);

  controller_update(&ctrl, 0.0f, 0.0f, 0.5f);
  check_near("torque_ramp.step1", ctrl.i_q_setpoint, 0.5f, 1e-4f);

  controller_update(&ctrl, 0.0f, 0.0f, 1.0f);
  check_near("torque_ramp.step2", ctrl.i_q_setpoint, 1.5f, 1e-4f);

  controller_update(&ctrl, 0.0f, 0.0f, 1.0f);
  check_near("torque_ramp.saturate", ctrl.i_q_setpoint, 2.0f, 1e-4f);
}

/* INPUT_MODE_INACTIVE: setpoints are held, new inputs are ignored. */
static void test_input_inactive(void)
{
  controller_t ctrl;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_INACTIVE);
  controller_set_input_vel(&ctrl, 3.0f);

  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("inactive.holds_setpoint", ctrl.vel_setpoint, 0.0f, 1e-6f);

  ctrl.vel_setpoint = 0.75f;
  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("inactive.keeps_setpoint", ctrl.vel_setpoint, 0.75f, 1e-6f);
}

/* INPUT_MODE_TRAP_TRAJ: the planner drives pos_setpoint to the new target. */
static void test_trap_traj(void)
{
  controller_t ctrl;
  int i;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_POSITION_CONTROL, INPUT_MODE_TRAP_TRAJ);
  ctrl.traj_vel_limit = 2.0f;
  ctrl.traj_accel_limit = 1.0f;
  ctrl.traj_decel_limit = 1.0f;
  controller_set_input_pos(&ctrl, 10.0f);

  /* trajectory_done is refreshed by the control update, not by the write. */
  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check("trap_traj.in_progress", !ctrl.trajectory_done);

  for (i = 0; i < 12000; i++)
  {
    controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  }

  check("trap_traj.done", ctrl.trajectory_done);
  check_near("trap_traj.pos_setpoint", ctrl.pos_setpoint, 10.0f, 0.05f);
  check("trap_traj.vel_setpoint_zero", fabsf(ctrl.vel_setpoint) < 0.05f);
}

/* Current loop: PID outputs are published as v_d/v_q. */
static void test_current_loop(void)
{
  controller_t ctrl;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  ctrl.i_d_setpoint = 1.0f;
  ctrl.i_q_setpoint = -0.5f;

  /* kp = 0.5, ki = 200, kd = 0 are the controller_init() current loop gains. */
  controller_update_current_references(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check_near("current_loop.vd", ctrl.v_d_setpoint, 0.7f, 1e-3f);
  check_near("current_loop.vq", ctrl.v_q_setpoint, -0.35f, 1e-3f);

  /* The voltage output is limited by the PID output bounds (+/- 12). */
  ctrl.i_d_setpoint = 100.0f;
  ctrl.i_q_setpoint = 0.0f;
  controller_update_current_references(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  check("current_loop.vd_limited", ctrl.v_d_setpoint <= 12.0f);
}

/* API contract: argument validation and the reset state. */
static void test_api_and_reset(void)
{
  controller_t ctrl;
  float iq_before;

  controller_init(&ctrl);
  controller_set_current_limits(&ctrl, 10.0f);
  controller_set_control_mode(&ctrl, CONTROL_MODE_TORQUE_CONTROL, INPUT_MODE_PASSTHROUGH);
  controller_set_input_torque(&ctrl, 4.0f);
  controller_update(&ctrl, 0.0f, 0.0f, 1.0e-3f);
  iq_before = ctrl.i_q_setpoint;

  controller_update(&ctrl, 0.0f, 0.0f, 0.0f);
  check_near("api.dt_zero_is_noop", ctrl.i_q_setpoint, iq_before, 1e-6f);

  controller_update(NULL, 0.0f, 0.0f, 1.0e-3f);
  controller_set_input_pos(NULL, 0.0f);
  controller_set_input_vel(NULL, 0.0f);
  controller_set_input_torque(NULL, 0.0f);
  controller_set_current_limits(NULL, 1.0f);
  controller_update_current_references(NULL, 0.0f, 0.0f, 1.0e-3f);
  controller_reset(NULL);

  controller_set_current_limits(&ctrl, -5.0f);
  check_near("api.negative_limit_clamped", ctrl.current_limit, 0.0f, 1e-6f);

  controller_reset(&ctrl);
  check_near("reset.iq", ctrl.i_q_setpoint, 0.0f, 1e-6f);
  check_near("reset.vq", ctrl.v_q_setpoint, 0.0f, 1e-6f);
  check_near("reset.pos_error", ctrl.pos_error, 0.0f, 1e-6f);
  check_near("reset.integrator", ctrl.current_q_pid.integrator, 0.0f, 1e-6f);
  check("reset.trajectory_done", ctrl.trajectory_done);
}

int main(void)
{
  g_failures = 0;

  test_torque_mode();
  test_velocity_mode();
  test_velocity_current_limit();
  test_position_mode();
  test_vel_ramp();
  test_torque_ramp();
  test_input_inactive();
  test_trap_traj();
  test_current_loop();
  test_api_and_reset();

  if (g_failures == 0)
  {
    printf("PASS test_controller\n");
    return 0;
  }

  printf("FAIL test_controller: %d checks failed\n", g_failures);
  return 1;
}
