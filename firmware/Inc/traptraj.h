#ifndef AT32_ODRIVE_TRAPTRAJ_H
#define AT32_ODRIVE_TRAPTRAJ_H

#include <stdbool.h>

typedef struct
{
  float vel_limit;
  float accel_limit;
  float decel_limit;
} traptraj_config_t;

typedef struct
{
  float y;
  float yd;
  float ydd;
} traptraj_step_t;

typedef struct
{
  traptraj_config_t config;

  float xi;
  float xf;
  float vi;

  float ar;
  float vr;
  float dr;

  float ta;
  float tv;
  float td;
  float tf;

  float y_accel_end;
} traptraj_t;

void traptraj_init(traptraj_t *traj, const traptraj_config_t *config);
bool traptraj_plan(traptraj_t *traj, float xf, float xi, float vi,
                   float vmax, float amax, float dmax);
traptraj_step_t traptraj_eval(const traptraj_t *traj, float t);
bool traptraj_done(const traptraj_t *traj, float t);

#endif /* AT32_ODRIVE_TRAPTRAJ_H */
