#ifndef AT32_ODRIVE_PID_H
#define AT32_ODRIVE_PID_H

#include <stdbool.h>

typedef struct
{
  float kp;
  float ki;
  float kd;
  float integrator;
  float prev_measurement;
  float output_min;
  float output_max;
  float integrator_min;
  float integrator_max;
  bool initialized;
} pid_t;

void pid_init(pid_t *pid, float kp, float ki, float kd,
              float output_min, float output_max,
              float integrator_min, float integrator_max);
void pid_reset(pid_t *pid);
float pid_update(pid_t *pid, float error, float dt);

#endif /* AT32_ODRIVE_PID_H */
