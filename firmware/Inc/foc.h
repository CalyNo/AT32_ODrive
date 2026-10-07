#ifndef AT32_ODRIVE_FOC_H
#define AT32_ODRIVE_FOC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  float a;
  float b;
  float c;
} foc_abc_t;

typedef struct
{
  float alpha;
  float beta;
} foc_alphabeta_t;

typedef struct
{
  float d;
  float q;
} foc_dq_t;

void foc_clarke(const foc_abc_t *in, foc_alphabeta_t *out);
void foc_park(const foc_alphabeta_t *in, foc_dq_t *out, float sin_theta, float cos_theta);
void foc_inverse_park(const foc_dq_t *in, foc_alphabeta_t *out, float sin_theta, float cos_theta);
void foc_svpwm(const foc_alphabeta_t *v_alphabeta, float vbus, float *duty_a, float *duty_b, float *duty_c);

float foc_wrap_0_2pi(float angle);
float foc_wrap_pm_pi(float angle);

#endif /* AT32_ODRIVE_FOC_H */
