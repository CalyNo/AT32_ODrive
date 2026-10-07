#include "foc.h"
#include "util.h"

#include <math.h>

void foc_clarke(const foc_abc_t *in, foc_alphabeta_t *out)
{
  /*
   * Amplitude-invariant Clarke transform:
   *   alpha = a
   *   beta  = (a + 2b) / sqrt(3)
   */
  out->alpha = in->a;
  out->beta = (in->a + 2.0f * in->b) * UTIL_INV_SQRT3_F;
}

void foc_park(const foc_alphabeta_t *in, foc_dq_t *out, float sin_theta, float cos_theta)
{
  out->d = in->alpha * cos_theta + in->beta * sin_theta;
  out->q = -in->alpha * sin_theta + in->beta * cos_theta;
}

void foc_inverse_park(const foc_dq_t *in, foc_alphabeta_t *out, float sin_theta, float cos_theta)
{
  out->alpha = in->d * cos_theta - in->q * sin_theta;
  out->beta = in->d * sin_theta + in->q * cos_theta;
}

void foc_svpwm(const foc_alphabeta_t *v_alphabeta, float vbus, float *duty_a, float *duty_b, float *duty_c)
{
  float va;
  float vb;
  float vc;
  float vmax;
  float vmin;
  float vcom;
  float inv_vbus;

  if (vbus < 1.0f)
  {
    vbus = 1.0f;
  }

  /* Inverse Clarke (amplitude invariant). */
  va = v_alphabeta->alpha;
  vb = -0.5f * v_alphabeta->alpha + UTIL_SQRT3_OVER_2_F * v_alphabeta->beta;
  vc = -0.5f * v_alphabeta->alpha - UTIL_SQRT3_OVER_2_F * v_alphabeta->beta;

  /* Common-mode injection gives the same linear range as SVPWM. */
  vmax = va;
  vmin = va;
  if (vb > vmax) vmax = vb;
  if (vc > vmax) vmax = vc;
  if (vb < vmin) vmin = vb;
  if (vc < vmin) vmin = vc;
  vcom = 0.5f * (vmax + vmin);

  inv_vbus = 1.0f / vbus;
  *duty_a = 0.5f + (va - vcom) * inv_vbus;
  *duty_b = 0.5f + (vb - vcom) * inv_vbus;
  *duty_c = 0.5f + (vc - vcom) * inv_vbus;

  if (*duty_a < 0.0f) *duty_a = 0.0f;
  if (*duty_a > 1.0f) *duty_a = 1.0f;
  if (*duty_b < 0.0f) *duty_b = 0.0f;
  if (*duty_b > 1.0f) *duty_b = 1.0f;
  if (*duty_c < 0.0f) *duty_c = 0.0f;
  if (*duty_c > 1.0f) *duty_c = 1.0f;
}

float foc_wrap_0_2pi(float angle)
{
  const float two_pi = 2.0f * UTIL_PI_F;
  angle = fmodf(angle, two_pi);
  if (angle < 0.0f)
  {
    angle += two_pi;
  }
  return angle;
}

float foc_wrap_pm_pi(float angle)
{
  const float pi = UTIL_PI_F;
  return foc_wrap_0_2pi(angle + pi) - pi;
}
