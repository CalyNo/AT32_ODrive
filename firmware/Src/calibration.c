#include "calibration.h"
#include "system_time.h"
#include "board.h"
#include "config.h"
#include "foc.h"
#include "nvm_config.h"
#include "pwm.h"
#include "util.h"

#include <math.h>
#include <stddef.h>

#define CAL_UPDATE_HZ          ((float)BOARD_VELOCITY_LOOP_FREQ_HZ)
#define CAL_UPDATE_PERIOD_S    (1.0f / CAL_UPDATE_HZ)
#define CAL_UPDATE_DELAY_US    (125u)

static void cal_read_dq(const axis_t *axis, float angle, float *i_d, float *i_q)
{
  foc_abc_t i_abc;
  foc_alphabeta_t i_ab;
  foc_dq_t i_dq;

  i_abc.a = axis->phase_current_a;
  i_abc.b = axis->phase_current_b;
  i_abc.c = axis->phase_current_c;
  foc_clarke(&i_abc, &i_ab);
  foc_park(&i_ab, &i_dq, sinf(angle), cosf(angle));

  if (i_d != NULL) *i_d = i_dq.d;
  if (i_q != NULL) *i_q = i_dq.q;
}

static void cal_encoder_update_for(axis_t *axis, float seconds)
{
  uint32_t count = (uint32_t)(seconds * CAL_UPDATE_HZ);

  for (uint32_t i = 0u; i < count; i++)
  {
    board_watchdog_feed();
    (void)encoder_update(&axis->encoder, CAL_UPDATE_PERIOD_S);
    system_delay_us(CAL_UPDATE_DELAY_US);
  }
}

static float cal_linear_slope(const float *x, const float *y, uint32_t count)
{
  float sum_x = 0.0f;
  float sum_y = 0.0f;
  float sum_xx = 0.0f;
  float sum_xy = 0.0f;
  float denom;
  float slope = 0.0f;

  if ((x == NULL) || (y == NULL) || (count < 2u))
  {
    return 0.0f;
  }

  for (uint32_t i = 0u; i < count; i++)
  {
    sum_x += x[i];
    sum_y += y[i];
    sum_xx += x[i] * x[i];
    sum_xy += x[i] * y[i];
  }

  denom = ((float)count * sum_xx) - (sum_x * sum_x);
  if (fabsf(denom) > 1.0e-9f)
  {
    slope = (((float)count * sum_xy) - (sum_x * sum_y)) / denom;
  }

  return slope;
}

bool calibration_motor_rl(axis_t *axis)
{
  float target_current;
  float max_voltage;
  float test_voltage = 0.05f;
  float current_d = 0.0f;
  float current_q = 0.0f;
  float current_average = 0.0f;
  float v_step;
  float t[12];
  float id[12];

  if (axis == NULL)
  {
    return false;
  }

  if (axis->vbus_voltage < 5.0f)
  {
    return false;
  }

  target_current = util_clampf(g_odrive_config.motor.calibration_current, 0.5f, 5.0f);
  max_voltage = util_clampf(g_odrive_config.motor.resistance_calib_max_voltage, 0.5f, 5.0f);

  pwm_enable();
  axis_set_calibration_active(true);
  pwm_set_zero_vector();
  system_delay_ms(20u);

  /* Ramp a d-axis voltage until the current target is reached. */
  for (uint32_t iter = 0u; iter < 40u; iter++)
  {
    board_watchdog_feed();
    axis_apply_voltage_vector(test_voltage, 0.0f, 0.0f, axis->vbus_voltage);
    system_delay_ms(20u);
    cal_read_dq(axis, 0.0f, &current_d, &current_q);

    if (fabsf(current_d) >= target_current)
    {
      break;
    }

    test_voltage += 0.05f;
    if (test_voltage > max_voltage)
    {
      test_voltage = max_voltage;
      break;
    }
  }

  if (fabsf(current_d) < 0.05f)
  {
    axis_set_calibration_active(false);
    pwm_set_zero_vector();
    pwm_disable();
    return false;
  }

  /* Average the current at the final voltage. */
  for (uint32_t i = 0u; i < 200u; i++)
  {
    board_watchdog_feed();
    system_delay_us(CAL_UPDATE_DELAY_US);
    cal_read_dq(axis, 0.0f, &current_d, &current_q);
    current_average += fabsf(current_d);
  }
  current_average /= 200.0f;

  if (current_average < 0.05f)
  {
    axis_set_calibration_active(false);
    pwm_set_zero_vector();
    pwm_disable();
    return false;
  }

  axis->phase_resistance = test_voltage / current_average;
  if ((axis->phase_resistance <= 1.0e-4f) || (axis->phase_resistance > 100.0f))
  {
    axis_set_calibration_active(false);
    pwm_set_zero_vector();
    pwm_disable();
    return false;
  }

  /*
   * L measurement: set current to zero, apply a small d-axis voltage step,
   * and fit the initial current slope.  L = V / (dI/dt).
   */
  axis_apply_voltage_vector(0.0f, 0.0f, 0.0f, axis->vbus_voltage);
  system_delay_ms(100u);

  v_step = util_clampf(axis->phase_resistance * 2.0f, 0.2f, 2.0f);
  axis_apply_voltage_vector(v_step, 0.0f, 0.0f, axis->vbus_voltage);

  for (uint32_t i = 0u; i < 12u; i++)
  {
    board_watchdog_feed();
    system_delay_us(CAL_UPDATE_DELAY_US);
    cal_read_dq(axis, 0.0f, &current_d, &current_q);
    t[i] = ((float)(i + 1u)) * 0.000125f;
    id[i] = fabsf(current_d);
  }

  {
    float slope = cal_linear_slope(t, id, 8u);
    if (slope <= 1.0f)
    {
      axis_set_calibration_active(false);
      pwm_set_zero_vector();
      pwm_disable();
      return false;
    }

    axis->phase_inductance = v_step / slope;
    if ((axis->phase_inductance <= 1.0e-7f) || (axis->phase_inductance > 0.1f))
    {
      axis_set_calibration_active(false);
      pwm_set_zero_vector();
      pwm_disable();
      return false;
    }
  }

  axis_set_calibration_active(false);
  pwm_set_zero_vector();
  pwm_disable();
  return true;
}

bool calibration_encoder_offset(axis_t *axis)
{
  const uint32_t steps = 48u;
  float align_voltage;
  float sum_x = 0.0f;
  float sum_y = 0.0f;
  float sum_xx = 0.0f;
  float sum_xy = 0.0f;
  float denom;
  float slope;
  float theta_last = 0.0f;
  float pole_pairs;

  if (axis == NULL)
  {
    return false;
  }

  if (axis->vbus_voltage < 5.0f)
  {
    return false;
  }

  align_voltage = util_clampf(axis->phase_resistance * 2.0f, 0.5f, 2.0f);
  pole_pairs = (float)axis->encoder.pole_pairs;

  pwm_enable();
  axis_set_calibration_active(true);
  pwm_set_zero_vector();
  system_delay_ms(20u);

  for (uint32_t i = 0u; i < steps; i++)
  {
    float theta = (2.0f * UTIL_PI_F * (float)i) / (float)steps;
    float measured;

    board_watchdog_feed();
    axis_apply_voltage_vector(align_voltage, 0.0f, theta, axis->vbus_voltage);
    cal_encoder_update_for(axis, 0.030f);

    measured = axis->encoder.pos_estimate * pole_pairs * 2.0f * UTIL_PI_F;

    sum_x += theta;
    sum_y += measured;
    sum_xx += theta * theta;
    sum_xy += theta * measured;
    theta_last = theta;
  }

  denom = ((float)steps * sum_xx) - (sum_x * sum_x);
  if (fabsf(denom) < 1.0e-6f)
  {
    axis_set_calibration_active(false);
    pwm_set_zero_vector();
    pwm_disable();
    return false;
  }

  slope = (((float)steps * sum_xy) - (sum_x * sum_y)) / denom;
  if (fabsf(slope) < 0.5f)
  {
    axis_set_calibration_active(false);
    pwm_set_zero_vector();
    pwm_disable();
    return false;
  }

  axis->encoder.direction = (slope >= 0.0f) ? 1.0f : -1.0f;

  /*
   * Re-seat the PLL count to the current absolute raw count, then choose the
   * electrical offset so the present rotor position matches the last applied
   * electrical angle.
   */
  encoder_set_linear_count(&axis->encoder, (int32_t)axis->encoder.raw);

  {
    float measured_now = axis->encoder.pos_estimate * pole_pairs * 2.0f * UTIL_PI_F;
    float offset = theta_last - axis->encoder.direction * measured_now;
    encoder_set_offset(&axis->encoder, foc_wrap_pm_pi(offset));
  }

  axis->encoder_offset_valid = true;
  axis_set_calibration_active(false);
  pwm_set_zero_vector();
  pwm_disable();
  return true;
}

bool calibration_process(axis_t *axis)
{
  bool ok = false;

  if ((axis == NULL) || (!axis->calibration_busy))
  {
    return false;
  }

  switch (axis->current_state)
  {
    case AXIS_STATE_MOTOR_CALIBRATION:
      ok = calibration_motor_rl(axis);
      if (ok)
      {
        axis->motor_calibrated = true;
      }
      break;

    case AXIS_STATE_ENCODER_OFFSET_CALIBRATION:
      ok = calibration_encoder_offset(axis);
      break;

    case AXIS_STATE_FULL_CALIBRATION_SEQUENCE:
      ok = calibration_motor_rl(axis);
      if (ok)
      {
        axis->motor_calibrated = true;
        ok = calibration_encoder_offset(axis);
      }
      break;

    default:
      ok = false;
      break;
  }

  axis_set_calibration_active(false);
  pwm_set_zero_vector();
  pwm_disable();

  axis->calibration_ok = axis->motor_calibrated && axis->encoder_offset_valid;
  axis->calibration_busy = false;

  if (ok)
  {
    g_odrive_config.motor.phase_resistance = axis->phase_resistance;
    g_odrive_config.motor.phase_inductance = axis->phase_inductance;
    g_odrive_config.encoder.direction = axis->encoder.direction;
    g_odrive_config.encoder.pos_offset = axis->encoder.pos_offset;
    if (axis->motor_calibrated)
    {
      g_odrive_config.motor.pre_calibrated = 1u;
    }
    if (axis->encoder_offset_valid)
    {
      g_odrive_config.encoder.pre_calibrated = 1u;
    }

    axis->current_state = AXIS_STATE_IDLE;
    axis->requested_state = AXIS_STATE_IDLE;
  }
  else
  {
    axis_set_error(axis, AXIS_ERROR_CALIBRATION_FAILED);
  }

  return ok;
}
