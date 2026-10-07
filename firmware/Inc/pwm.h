#ifndef AT32_ODRIVE_PWM_H
#define AT32_ODRIVE_PWM_H

#include <stdbool.h>
#include <stdint.h>

void pwm_init(void);
void pwm_enable(void);
void pwm_disable(void);
bool pwm_is_enabled(void);

/* Duty values are normalized to [0, 1]. 0.5 is the zero vector. */
void pwm_set_duty(float duty_a, float duty_b, float duty_c);
void pwm_set_zero_vector(void);

uint16_t pwm_duty_to_ticks(float duty);

#endif /* AT32_ODRIVE_PWM_H */
