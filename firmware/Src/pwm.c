#include "pwm.h"
#include "board.h"
#include "config.h"

#include <stddef.h>

#define PWM_ADC_TRIGGER_DELAY_NS   (400u)

static uint16_t s_pwm_period;
static uint16_t s_adc_trig_ticks;
static bool s_enabled;

uint16_t pwm_duty_to_ticks(float duty)
{
  uint32_t ticks;

  if (duty <= 0.0f)
  {
    return 0u;
  }
  if (duty >= 1.0f)
  {
    return s_pwm_period;
  }

  ticks = (uint32_t)(duty * (float)s_pwm_period + 0.5f);
  if (ticks > s_pwm_period)
  {
    ticks = s_pwm_period;
  }
  return (uint16_t)ticks;
}

void pwm_init(void)
{
  crm_clocks_freq_type clocks = {0};
  tmr_output_config_type oc = {0};
  tmr_brkdt_config_type brkdt = {0};
  uint32_t tmr_clk;

  crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);

  crm_clocks_freq_get(&clocks);
  tmr_clk = clocks.apb2_freq * 2u;
  s_pwm_period = (uint16_t)(tmr_clk / (2u * BOARD_PWM_FREQ_HZ));
  if (s_pwm_period == 0u)
  {
    s_pwm_period = 1u;
  }

  /* ADC sample point ~0.4 us before the center-aligned peak, as in Artery MCLIB. */
  s_adc_trig_ticks = (uint16_t)((tmr_clk / 1000000u) * PWM_ADC_TRIGGER_DELAY_NS / 1000u);
  if (s_adc_trig_ticks >= s_pwm_period)
  {
    s_adc_trig_ticks = 0u;
  }

  /* Repetition counter = 1 makes the PWM cycle/update event occur once per period. */
  tmr_repetition_counter_set(TMR1, 1u);
  tmr_base_init(TMR1, s_pwm_period, 0u);
  tmr_cnt_dir_set(TMR1, TMR_COUNT_TWO_WAY_1);
  tmr_clock_source_div_set(TMR1, TMR_CLOCK_DIV4);

  tmr_output_default_para_init(&oc);
  oc.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  oc.oc_output_state = TRUE;
  oc.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  oc.oc_idle_state = FALSE;
  oc.occ_output_state = TRUE;
  oc.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  oc.occ_idle_state = FALSE;

  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_1, &oc);
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_2, &oc);
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_3, &oc);

  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, s_pwm_period / 2u);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, s_pwm_period / 2u);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_3, s_pwm_period / 2u);

  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_1, TRUE);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_2, TRUE);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_3, TRUE);

  /* CH4 is an internal ADC trigger, not a pin output. */
  oc.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  oc.oc_output_state = FALSE;
  oc.occ_output_state = FALSE;
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_4, &oc);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_4, (uint16_t)(s_pwm_period - 1u - s_adc_trig_ticks));

  tmr_brkdt_default_para_init(&brkdt);
  /* The dead-time generator runs from the timer clock divided by
   * TMR_CLOCK_DIV4; BOARD_PWM_DEADTIME_TICKS is expressed in those units. */
  brkdt.deadtime = (uint8_t)BOARD_PWM_DEADTIME_TICKS;
  brkdt.brk_enable = FALSE;
  brkdt.auto_output_enable = FALSE;
  brkdt.wp_level = TMR_WP_OFF;
  tmr_brkdt_config(TMR1, &brkdt);

  pwm_set_zero_vector();
  s_enabled = false;
}

void pwm_enable(void)
{
  /*
   * Single gate for the power stage: refuse to switch the bridge on when the
   * system clock is not the HEXT-driven 288 MHz.  The timer periods (PWM
   * frequency, dead time, ADC sample point) are all derived from it, and the
   * HICK fallback is only ~+-2-3% accurate, so the current limits and the
   * sample timing would be unreliable.  Calibration and closed-loop both go
   * through here, so this covers every path.
   */
  if (board_clock_is_degraded())
  {
    return;
  }

  if (s_enabled)
  {
    return;
  }

  pwm_set_zero_vector();
  tmr_output_enable(TMR1, TRUE);
  tmr_counter_enable(TMR1, TRUE);
  s_enabled = true;
}

void pwm_disable(void)
{
  if (!s_enabled)
  {
    return;
  }

  pwm_set_zero_vector();
  tmr_output_enable(TMR1, FALSE);
  tmr_counter_enable(TMR1, FALSE);
  s_enabled = false;
}

bool pwm_is_enabled(void)
{
  return s_enabled;
}

void pwm_set_duty(float duty_a, float duty_b, float duty_c)
{
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, pwm_duty_to_ticks(duty_a));
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, pwm_duty_to_ticks(duty_b));
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_3, pwm_duty_to_ticks(duty_c));
}

void pwm_set_zero_vector(void)
{
  pwm_set_duty(0.5f, 0.5f, 0.5f);
}
