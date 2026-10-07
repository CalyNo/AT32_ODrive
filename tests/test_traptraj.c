#include "traptraj.h"

#include <math.h>
#include <stdio.h>

static int nearf(float a, float b, float tol)
{
  return fabsf(a - b) <= tol;
}

int main(void)
{
  traptraj_t traj;
  traptraj_config_t config = {2.0f, 1.0f, 1.0f};
  traptraj_step_t step;
  int failures = 0;

  traptraj_init(&traj, &config);
  if (!traptraj_plan(&traj, 10.0f, 0.0f, 0.0f, 2.0f, 1.0f, 1.0f))
  {
    printf("FAIL traptraj plan\n");
    return 1;
  }

  if (!nearf(traj.ta, 2.0f, 1e-3f) ||
      !nearf(traj.tv, 3.0f, 1e-3f) ||
      !nearf(traj.td, 2.0f, 1e-3f) ||
      !nearf(traj.tf, 7.0f, 1e-3f))
  {
    printf("FAIL traptraj phase durations: ta=%.4f tv=%.4f td=%.4f tf=%.4f\n",
           traj.ta, traj.tv, traj.td, traj.tf);
    failures++;
  }

  step = traptraj_eval(&traj, 0.0f);
  failures += nearf(step.y, 0.0f, 1e-3f) ? 0 : 1;
  failures += nearf(step.yd, 0.0f, 1e-3f) ? 0 : 1;

  step = traptraj_eval(&traj, 2.0f);
  failures += nearf(step.y, 2.0f, 1e-3f) ? 0 : 1;
  failures += nearf(step.yd, 2.0f, 1e-3f) ? 0 : 1;

  step = traptraj_eval(&traj, 5.0f);
  failures += nearf(step.y, 8.0f, 1e-3f) ? 0 : 1;
  failures += nearf(step.yd, 2.0f, 1e-3f) ? 0 : 1;

  step = traptraj_eval(&traj, 7.0f);
  failures += nearf(step.y, 10.0f, 1e-3f) ? 0 : 1;
  failures += nearf(step.yd, 0.0f, 1e-3f) ? 0 : 1;
  failures += traptraj_done(&traj, 7.0f) ? 0 : 1;

  if (!traptraj_plan(&traj, 1.0f, 0.0f, 0.0f, 10.0f, 1.0f, 1.0f))
  {
    printf("FAIL traptraj triangular plan\n");
    return 1;
  }
  step = traptraj_eval(&traj, traj.tf);
  failures += nearf(step.y, 1.0f, 1e-3f) ? 0 : 1;
  failures += nearf(step.yd, 0.0f, 1e-3f) ? 0 : 1;
  if (traj.tv > 1e-6f)
  {
    printf("FAIL traptraj triangular should not cruise: tv=%.6f\n", traj.tv);
    failures++;
  }

  if (failures == 0)
  {
    printf("PASS test_traptraj\n");
    return 0;
  }

  printf("FAIL test_traptraj: %d checks failed\n", failures);
  return 1;
}
