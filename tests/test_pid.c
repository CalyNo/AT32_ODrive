#include "pid.h"

#include <math.h>
#include <stdio.h>

int main(void)
{
  pid_t pid;
  float output = 0.0f;

  pid_init(&pid, 1.0f, 10.0f, 0.0f, -10.0f, 10.0f, -5.0f, 5.0f);

  /* Positive error should produce a bounded positive output. */
  output = pid_update(&pid, 1.0f, 0.001f);
  if (output <= 0.0f || output > 10.0f)
  {
    printf("FAIL pid positive response: %.6f\n", output);
    return 1;
  }

  /* Large persistent error should saturate but remain bounded. */
  for (int i = 0; i < 10000; i++)
  {
    output = pid_update(&pid, 1000.0f, 0.001f);
  }
  if (output > 10.0f || pid.integrator > 5.0f)
  {
    printf("FAIL pid saturation: out=%.6f integrator=%.6f\n", output, pid.integrator);
    return 1;
  }

  pid_reset(&pid);
  if (fabsf(pid.integrator) > 1e-6f)
  {
    printf("FAIL pid reset\n");
    return 1;
  }

  printf("PASS test_pid\n");
  return 0;
}
