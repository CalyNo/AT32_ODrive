#include "encoder_pll.h"

#include "foc.h"
#include "util.h"

#include <math.h>
#include <stddef.h>

/*
 * Pure estimator half of the encoder module: no CMSIS, no SPI, no globals.
 * The implementation is the phase-locked loop that turns absolute MT6816
 * counts into a continuous position, a velocity estimate and an electrical
 * angle for the field-oriented control loops.
 */

void encoder_pll_init(encoder_t *enc, encoder_type_t type, float cpr, float direction,
                      int32_t pole_pairs, float bandwidth_hz)
{
  if (enc == NULL)
  {
    return;
  }

  enc->type = type;
  enc->pole_pairs = (pole_pairs > 0) ? pole_pairs : 1;
  enc->cpr = (cpr > 0.0f) ? cpr : 1.0f;
  enc->direction = (direction >= 0.0f) ? 1.0f : -1.0f;
  enc->pos_estimate = 0.0f;
  enc->vel_estimate = 0.0f;
  enc->pos_estimate_counts = 0.0f;
  enc->vel_estimate_counts = 0.0f;
  enc->shadow_count = 0;
  enc->pos_offset = 0.0f;
  enc->phase_offset_counts = 0.0f;
  enc->interpolation = 0.5f;
  enc->raw = 0u;
  enc->last_raw = 0u;
  enc->electrical_angle = 0.0f;
  enc->error_count = 0u;
  enc->magnet_ok = true;
  enc->initialized = false;
  enc->index_found = false;
  enc->pos_estimate_valid = false;
  enc->vel_estimate_valid = false;

  encoder_set_bandwidth(enc, bandwidth_hz);
}

void encoder_set_bandwidth(encoder_t *enc, float bandwidth_hz)
{
  if ((enc == NULL) || (bandwidth_hz <= 0.0f))
  {
    return;
  }

  enc->bandwidth = bandwidth_hz;
  enc->pll_kp = 2.0f * bandwidth_hz;
  enc->pll_ki = 0.25f * enc->pll_kp * enc->pll_kp;
}

bool encoder_pll_update(encoder_t *enc, uint16_t raw, float dt)
{
  int32_t delta_signed;
  int32_t half;
  float encoder_model;
  float delta_pos;
  float dtf;

  if ((enc == NULL) || (dt <= 0.0f))
  {
    return false;
  }

  if (!enc->initialized)
  {
    enc->last_raw = raw;
    enc->raw = raw;
    enc->shadow_count = (int32_t)raw;
    enc->pos_estimate_counts = (float)raw;
    enc->vel_estimate_counts = 0.0f;
    enc->pos_estimate = (float)raw / enc->cpr;
    enc->vel_estimate = 0.0f;
    enc->initialized = true;
    enc->pos_estimate_valid = true;
    enc->vel_estimate_valid = true;
    return true;
  }

  half = (int32_t)(enc->cpr * 0.5f);
  delta_signed = (int32_t)raw - (int32_t)enc->last_raw;
  if (delta_signed > half)
  {
    delta_signed -= (int32_t)enc->cpr;
  }
  else if (delta_signed < -half)
  {
    delta_signed += (int32_t)enc->cpr;
  }

  if (enc->direction < 0.0f)
  {
    delta_signed = -delta_signed;
  }

  enc->last_raw = raw;
  enc->raw = raw;
  enc->shadow_count += delta_signed;

  dtf = dt;
  if ((dtf * enc->pll_kp) >= 1.0f)
  {
    enc->error_count++;
    enc->pos_estimate_valid = false;
    return false;
  }

  /* Predict. */
  enc->pos_estimate_counts += dtf * enc->vel_estimate_counts;

  /* Phase detector: compare measured count against PLL model. */
  encoder_model = floorf(enc->pos_estimate_counts);
  delta_pos = (float)(enc->shadow_count - (int32_t)encoder_model);
  if ((enc->cpr > 1.0f) && (fabsf(delta_pos) > (enc->cpr * 0.5f)))
  {
    /* Guard against startup offset jumps. */
    enc->pos_estimate_counts = (float)enc->shadow_count;
    delta_pos = 0.0f;
  }

  /* Corrections. */
  enc->pos_estimate_counts += dtf * enc->pll_kp * delta_pos;
  enc->vel_estimate_counts += dtf * enc->pll_ki * delta_pos;

  if (fabsf(enc->vel_estimate_counts) < (0.5f * dtf * enc->pll_ki))
  {
    enc->vel_estimate_counts = 0.0f;
  }

  enc->pos_estimate = enc->pos_estimate_counts / enc->cpr;
  enc->vel_estimate = enc->vel_estimate_counts / enc->cpr;
  enc->pos_estimate_valid = true;
  enc->vel_estimate_valid = true;

  /* Phase interpolation between absolute counts. */
  if (delta_signed > 0)
  {
    enc->interpolation = 0.0f;
  }
  else if (delta_signed < 0)
  {
    enc->interpolation = 1.0f;
  }
  else
  {
    enc->interpolation += dtf * enc->vel_estimate_counts;
    if (enc->interpolation > 1.0f)
    {
      enc->interpolation = 1.0f;
    }
    if (enc->interpolation < 0.0f)
    {
      enc->interpolation = 0.0f;
    }
  }

  {
    float interpolated_count = (float)enc->shadow_count + enc->interpolation - enc->phase_offset_counts;

    enc->electrical_angle = foc_wrap_0_2pi(
        (interpolated_count / enc->cpr) * (float)enc->pole_pairs * 2.0f * UTIL_PI_F +
        enc->pos_offset);
  }

  return true;
}

float encoder_get_electrical_angle(const encoder_t *enc, float pole_pairs)
{
  if (enc == NULL)
  {
    return 0.0f;
  }

  return foc_wrap_0_2pi(enc->pos_estimate * pole_pairs * 2.0f * UTIL_PI_F + enc->pos_offset);
}

void encoder_set_linear_count(encoder_t *enc, int32_t count)
{
  int32_t raw_count;

  if ((enc == NULL) || (enc->cpr <= 0.0f))
  {
    return;
  }

  raw_count = count % (int32_t)enc->cpr;
  if (raw_count < 0)
  {
    raw_count += (int32_t)enc->cpr;
  }

  enc->shadow_count = count;
  enc->pos_estimate_counts = (float)count;
  enc->vel_estimate_counts = 0.0f;
  enc->pos_estimate = (float)count / enc->cpr;
  enc->vel_estimate = 0.0f;
  enc->last_raw = (uint16_t)raw_count;
  enc->raw = (uint16_t)raw_count;
  enc->initialized = true;
}

void encoder_set_offset(encoder_t *enc, float offset_rad)
{
  if (enc != NULL)
  {
    enc->pos_offset = offset_rad;
  }
}
