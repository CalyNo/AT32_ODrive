#include "pid.h"

#include <stddef.h>

void pid_init(pid_t *pid, float kp, float ki, float kd,
              float output_min, float output_max,
              float integrator_min, float integrator_max)
{
  if (pid == NULL)
  {
    return;
  }

  pid->kp = kp;
  pid->ki = ki;
  pid->kd = kd;
  pid->integrator = 0.0f;
  pid->prev_measurement = 0.0f;
  pid->output_min = output_min;
  pid->output_max = output_max;
  pid->integrator_min = integrator_min;
  pid->integrator_max = integrator_max;
  pid->initialized = true;
}

void pid_reset(pid_t *pid)
{
  if (pid == NULL)
  {
    return;
  }

  pid->integrator = 0.0f;
  pid->prev_measurement = 0.0f;
}

float pid_update(pid_t *pid, float error, float dt)
{
  float p_term;
  float d_term;
  float out;

  if ((pid == NULL) || (!pid->initialized) || (dt <= 0.0f))
  {
    return 0.0f;
  }

  p_term = pid->kp * error;

  /* Integrate. */
  pid->integrator += pid->ki * error * dt;
  if (pid->integrator > pid->integrator_max)
  {
    pid->integrator = pid->integrator_max;
  }
  else if (pid->integrator < pid->integrator_min)
  {
    pid->integrator = pid->integrator_min;
  }

  /* Derivative on error with a simple first difference. */
  d_term = pid->kd * (error - pid->prev_measurement) / dt;
  pid->prev_measurement = error;

  out = p_term + pid->integrator + d_term;
  if (out > pid->output_max)
  {
    out = pid->output_max;
  }
  else if (out < pid->output_min)
  {
    out = pid->output_min;
  }

  return out;
}
