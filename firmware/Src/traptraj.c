#include "traptraj.h"

#include <math.h>
#include <stddef.h>

static float traj_sign(float value)
{
  return (value < 0.0f) ? -1.0f : 1.0f;
}

static float traj_square(float value)
{
  return value * value;
}

void traptraj_init(traptraj_t *traj, const traptraj_config_t *config)
{
  if (traj == NULL)
  {
    return;
  }

  traj->config.vel_limit = (config != NULL) ? config->vel_limit : 1.0f;
  traj->config.accel_limit = (config != NULL) ? config->accel_limit : 1.0f;
  traj->config.decel_limit = (config != NULL) ? config->decel_limit : 1.0f;

  if (traj->config.vel_limit <= 0.0f)
  {
    traj->config.vel_limit = 1.0f;
  }
  if (traj->config.accel_limit <= 0.0f)
  {
    traj->config.accel_limit = 1.0f;
  }
  if (traj->config.decel_limit <= 0.0f)
  {
    traj->config.decel_limit = 1.0f;
  }

  traj->xi = 0.0f;
  traj->xf = 0.0f;
  traj->vi = 0.0f;
  traj->ar = 0.0f;
  traj->vr = 0.0f;
  traj->dr = 0.0f;
  traj->ta = 0.0f;
  traj->tv = 0.0f;
  traj->td = 0.0f;
  traj->tf = 0.0f;
  traj->y_accel_end = 0.0f;
}

bool traptraj_plan(traptraj_t *traj, float xf, float xi, float vi,
                   float vmax, float amax, float dmax)
{
  float dx;
  float stop_distance;
  float dx_stop;
  float direction;
  float dx_min;
  float discriminant;

  if (traj == NULL)
  {
    return false;
  }

  if (vmax <= 0.0f) vmax = traj->config.vel_limit;
  if (amax <= 0.0f) amax = traj->config.accel_limit;
  if (dmax <= 0.0f) dmax = traj->config.decel_limit;

  dx = xf - xi;

  /* Minimum distance needed to stop at the current speed. */
  stop_distance = (vi * vi) / (2.0f * dmax);
  dx_stop = traj_sign(vi) * stop_distance;

  /* Direction of the cruise phase. */
  direction = traj_sign(dx - dx_stop);
  traj->ar = direction * amax;
  traj->dr = -direction * dmax;
  traj->vr = direction * vmax;

  /* If already faster than the target cruise velocity, decelerate instead. */
  if ((direction * vi) > (direction * traj->vr))
  {
    traj->ar = -direction * amax;
  }

  traj->ta = (traj->vr - vi) / traj->ar;
  traj->td = -traj->vr / traj->dr;

  dx_min = 0.5f * traj->ta * (traj->vr + vi) + 0.5f * traj->td * traj->vr;

  if ((direction * dx) < (direction * dx_min))
  {
    /* Triangular profile: cruise velocity is never reached. */
    discriminant = (traj->dr * traj_square(vi) + 2.0f * traj->ar * traj->dr * dx) /
                   (traj->dr - traj->ar);
    if (discriminant < 0.0f)
    {
      discriminant = 0.0f;
    }

    traj->vr = direction * sqrtf(discriminant);
    traj->ta = (traj->vr - vi) / traj->ar;
    if (traj->ta < 0.0f) traj->ta = 0.0f;
    traj->td = -traj->vr / traj->dr;
    if (traj->td < 0.0f) traj->td = 0.0f;
    traj->tv = 0.0f;
  }
  else
  {
    if (fabsf(traj->vr) < 1.0e-9f)
    {
      traj->tv = 0.0f;
    }
    else
    {
      traj->tv = (dx - dx_min) / traj->vr;
    }
  }

  traj->tf = traj->ta + traj->tv + traj->td;
  traj->xi = xi;
  traj->xf = xf;
  traj->vi = vi;
  traj->y_accel_end = xi + vi * traj->ta + 0.5f * traj->ar * traj_square(traj->ta);

  return true;
}

traptraj_step_t traptraj_eval(const traptraj_t *traj, float t)
{
  traptraj_step_t step = {0.0f, 0.0f, 0.0f};

  if (traj == NULL)
  {
    return step;
  }

  if (t <= 0.0f)
  {
    step.y = traj->xi;
    step.yd = traj->vi;
    step.ydd = 0.0f;
  }
  else if (t < traj->ta)
  {
    step.y = traj->xi + traj->vi * t + 0.5f * traj->ar * traj_square(t);
    step.yd = traj->vi + traj->ar * t;
    step.ydd = traj->ar;
  }
  else if (t < (traj->ta + traj->tv))
  {
    step.y = traj->y_accel_end + traj->vr * (t - traj->ta);
    step.yd = traj->vr;
    step.ydd = 0.0f;
  }
  else if (t < traj->tf)
  {
    float td = t - traj->tf;
    step.y = traj->xf + 0.5f * traj->dr * traj_square(td);
    step.yd = traj->dr * td;
    step.ydd = traj->dr;
  }
  else
  {
    step.y = traj->xf;
    step.yd = 0.0f;
    step.ydd = 0.0f;
  }

  return step;
}

bool traptraj_done(const traptraj_t *traj, float t)
{
  if (traj == NULL)
  {
    return true;
  }
  return t >= traj->tf;
}
