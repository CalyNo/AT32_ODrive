/*
 * Host-side tests for the encoder PLL estimator (encoder_pll.c).
 *
 * The estimator is the part of the encoder module that turns absolute MT6816
 * counts into position, velocity and electrical angle, so it is exercised here
 * with a synthetic encoder that produces ideal counts for a commanded speed.
 */
#include "encoder_pll.h"

#include <math.h>
#include <stdio.h>

#define TEST_CPR       (16384.0f)
#define TEST_DT        (1.0f / 8000.0f)

static int g_failures;

static void check(const char *name, int ok)
{
  if (!ok)
  {
    printf("FAIL %s\n", name);
    g_failures++;
  }
}

static void check_near(const char *name, float actual, float expected, float tol)
{
  if (fabsf(actual - expected) > tol)
  {
    printf("FAIL %s: actual=%.6f expected=%.6f tol=%.6f\n", name, actual, expected, tol);
    g_failures++;
  }
}

static void encoder_reset(encoder_t *enc, float direction)
{
  /* Same starting state as encoder_init(), without touching the SPI hardware. */
  encoder_pll_init(enc, ENCODER_TYPE_MT6816, TEST_CPR, direction, 1,
                   ENCODER_PLL_DEFAULT_BANDWIDTH_HZ);
}

/* The first sample only seeds the state. */
static void test_initialization(void)
{
  encoder_t enc;

  encoder_reset(&enc, 1.0f);
  check("init.not_initialized", !enc.initialized);
  check("init.angle_zero", enc.electrical_angle == 0.0f);

  check("init.update_ok", encoder_pll_update(&enc, 4096u, TEST_DT));
  check("init.initialized", enc.initialized);
  check("init.pos_estimate_valid", enc.pos_estimate_valid);
  check("init.vel_estimate_valid", enc.vel_estimate_valid);
  check_near("init.pos_estimate", enc.pos_estimate, 4096.0f / TEST_CPR, 1e-6f);
  check_near("init.vel_estimate", enc.vel_estimate, 0.0f, 1e-6f);
  check("init.shadow_count", enc.shadow_count == 4096);
}

/* Constant speed: the PLL must converge to the true velocity with no error. */
static void test_constant_velocity(void)
{
  encoder_t enc;
  float true_counts = 0.0f;
  const float vel_rev_s = 1.0f;
  const float counts_per_sample = TEST_CPR * vel_rev_s * TEST_DT;
  int i;

  encoder_reset(&enc, 1.0f);
  (void)encoder_pll_update(&enc, 0u, TEST_DT);

  for (i = 0; i < 4000; i++)
  {
    true_counts += counts_per_sample;
    (void)encoder_pll_update(&enc, (uint16_t)((int32_t)floorf(true_counts) & 0x3FFFu), TEST_DT);
  }

  check_near("cw.vel_estimate", enc.vel_estimate, vel_rev_s, 0.05f);
  check_near("cw.pos_estimate", enc.pos_estimate, true_counts / TEST_CPR, 0.02f);
  check("cw.shadow_count_matches", enc.shadow_count == (int32_t)floorf(true_counts));
}

/* direction = -1 mirrors the count direction. */
static void test_direction(void)
{
  encoder_t enc;
  int i;

  encoder_reset(&enc, -1.0f);
  (void)encoder_pll_update(&enc, 0u, TEST_DT);

  for (i = 0; i < 100; i++)
  {
    (void)encoder_pll_update(&enc, (uint16_t)((i + 1) * 10), TEST_DT);
  }

  check("ccw.shadow_count", enc.shadow_count == -1000);
  check("ccw.pos_estimate_negative", enc.pos_estimate < 0.0f);
  check("ccw.vel_estimate_negative", enc.vel_estimate < 0.0f);
}

/* The count is 14 bit, so the estimator must unwrap the wrap-around. */
static void test_wrap_around(void)
{
  encoder_t enc;

  encoder_reset(&enc, 1.0f);
  (void)encoder_pll_update(&enc, 16383u, TEST_DT);

  (void)encoder_pll_update(&enc, 1u, TEST_DT); /* +2 across the boundary */
  check("wrap.forward_shadow_count", enc.shadow_count == 16385);

  (void)encoder_pll_update(&enc, 0u, TEST_DT); /* -1 back across the boundary */
  check("wrap.backward_shadow_count", enc.shadow_count == 16384);
}

/* Electrical angle: mechanical position scaled by pole pairs, inside [0, 2pi). */
static void test_electrical_angle(void)
{
  encoder_t enc;
  const float two_pi = 2.0f * (float)M_PI;

  encoder_reset(&enc, 1.0f);
  (void)encoder_pll_update(&enc, 0u, TEST_DT);
  (void)encoder_pll_update(&enc, 64u, TEST_DT);

  check_near("angle.quarter_count", enc.electrical_angle, 64.0f / TEST_CPR * two_pi, 1e-3f);
  check("angle.in_range", (enc.electrical_angle >= 0.0f) && (enc.electrical_angle < two_pi));

  /* encoder_get_electrical_angle() is the pure helper used by the axis loop. */
  enc.pos_estimate = 0.25f;
  enc.pos_offset = 0.0f;
  check_near("angle.helper_quarter_rev", encoder_get_electrical_angle(&enc, 1.0f), 0.25f * two_pi, 1e-4f);

  /* One pole pair and half a mechanical revolution give pi. */
  check_near("angle.helper_half_rev", encoder_get_electrical_angle(&enc, 2.0f), 0.5f * two_pi, 1e-4f);

  enc.pos_offset = 0.5f;
  check_near("angle.helper_offset", encoder_get_electrical_angle(&enc, 1.0f), 0.25f * two_pi + 0.5f, 1e-4f);
}

/* A sample that the PLL cannot use must be reported, not silently accepted. */
static void test_bandwidth_guard(void)
{
  encoder_t enc;
  uint32_t errors_before;

  encoder_reset(&enc, 1.0f);
  (void)encoder_pll_update(&enc, 0u, TEST_DT);

  /* dt * pll_kp = 1e-3 * 40000 = 40 >> 1, so the loop is unusable. */
  encoder_set_bandwidth(&enc, 20000.0f);
  errors_before = enc.error_count;

  check("guard.rejects", !encoder_pll_update(&enc, 100u, 1.0e-3f));
  check("guard.clears_valid", !enc.pos_estimate_valid);
  check("guard.counts_error", enc.error_count == (errors_before + 1u));

  /* A zero/negative bandwidth must be ignored, not applied. */
  encoder_set_bandwidth(&enc, -1.0f);
  check_near("guard.bandwidth_unchanged", enc.bandwidth, 20000.0f, 1e-3f);

  /* Rejecting the sample must not corrupt the usable configuration. */
  encoder_set_bandwidth(&enc, 1000.0f);
  check("guard.usable_again", encoder_pll_update(&enc, 101u, TEST_DT));
  check_near("guard.pll_kp", enc.pll_kp, 2000.0f, 1e-3f);
  check_near("guard.pll_ki", enc.pll_ki, 1.0e6f, 1.0f);
}

/* Linear count injection is used by the CAN SET_LINEAR_COUNT command. */
static void test_set_linear_count(void)
{
  encoder_t enc;

  encoder_reset(&enc, 1.0f);
  (void)encoder_pll_update(&enc, 0u, TEST_DT);

  encoder_set_linear_count(&enc, 20000);
  check("linear_count.shadow", enc.shadow_count == 20000);
  check_near("linear_count.pos", enc.pos_estimate, 20000.0f / TEST_CPR, 1e-6f);
  check_near("linear_count.vel", enc.vel_estimate, 0.0f, 1e-6f);
  check("linear_count.raw_wrapped", enc.raw == (uint16_t)(20000 % 16384));
  check_near("linear_count.interpolation_untouched", enc.interpolation, 0.5f, 1e-6f);

  /* Negative counts wrap into the raw range as well. */
  encoder_set_linear_count(&enc, -100);
  check("linear_count.negative_shadow", enc.shadow_count == -100);

  /* Guard against a non-positive CPR (division by zero). */
  enc.cpr = 0.0f;
  encoder_set_linear_count(&enc, 1234);
  check("linear_count.cpr_guard", enc.shadow_count == -100);
}

/* Argument validation must not crash or corrupt state. */
static void test_api_guard(void)
{
  encoder_t enc;
  float pos_before;

  encoder_reset(&enc, 1.0f);
  (void)encoder_pll_update(&enc, 0u, TEST_DT);
  pos_before = enc.pos_estimate;

  check("api.null_update", !encoder_pll_update(NULL, 0u, TEST_DT));
  check("api.zero_dt", !encoder_pll_update(&enc, 10u, 0.0f));
  check("api.negative_dt", !encoder_pll_update(&enc, 10u, -1.0f));
  check_near("api.state_unchanged", enc.pos_estimate, pos_before, 1e-9f);

  encoder_set_offset(NULL, 0.0f);
  encoder_set_bandwidth(NULL, 100.0f);
  encoder_set_linear_count(NULL, 0);
  check_near("api.null_angle", encoder_get_electrical_angle(NULL, 1.0f), 0.0f, 1e-6f);
}

int main(void)
{
  g_failures = 0;

  test_initialization();
  test_constant_velocity();
  test_direction();
  test_wrap_around();
  test_electrical_angle();
  test_bandwidth_guard();
  test_set_linear_count();
  test_api_guard();

  if (g_failures == 0)
  {
    printf("PASS test_encoder_pll\n");
    return 0;
  }

  printf("FAIL test_encoder_pll: %d checks failed\n", g_failures);
  return 1;
}
