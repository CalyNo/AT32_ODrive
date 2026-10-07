#include "foc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(const char *name, float actual, float expected, float tol)
{
  if (fabsf(actual - expected) > tol)
  {
    printf("FAIL %s: actual=%.6f expected=%.6f tol=%.6f\n", name, actual, expected, tol);
    return 1;
  }
  return 0;
}

int main(void)
{
  int failures = 0;
  foc_abc_t abc;
  foc_alphabeta_t ab;
  foc_dq_t dq;
  foc_alphabeta_t ab2;
  float duty_a;
  float duty_b;
  float duty_c;
  float theta = 0.37f;
  float sin_theta = sinf(theta);
  float cos_theta = cosf(theta);

  abc.a = 1.0f;
  abc.b = -0.5f;
  abc.c = -0.5f;
  foc_clarke(&abc, &ab);
  failures += fail("clarke.alpha", ab.alpha, 1.0f, 1e-5f);
  failures += fail("clarke.beta", ab.beta, 0.0f, 1e-5f);

  foc_park(&ab, &dq, sin_theta, cos_theta);
  foc_inverse_park(&dq, &ab2, sin_theta, cos_theta);
  failures += fail("park_roundtrip.alpha", ab2.alpha, ab.alpha, 1e-5f);
  failures += fail("park_roundtrip.beta", ab2.beta, ab.beta, 1e-5f);

  ab.alpha = 1.2f;
  ab.beta = -0.4f;
  foc_svpwm(&ab, 12.0f, &duty_a, &duty_b, &duty_c);
  failures += (duty_a < 0.0f || duty_a > 1.0f) ? 1 : 0;
  failures += (duty_b < 0.0f || duty_b > 1.0f) ? 1 : 0;
  failures += (duty_c < 0.0f || duty_c > 1.0f) ? 1 : 0;

  if (failures == 0)
  {
    printf("PASS test_foc\n");
    return 0;
  }

  printf("FAIL test_foc: %d checks failed\n", failures);
  return 1;
}
