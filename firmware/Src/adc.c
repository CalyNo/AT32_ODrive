#include "adc.h"
#include "irq_priority.h"
#include "board.h"
#include "config.h"

#include <stddef.h>

/* Poll bound for a software triggered ordinary conversion (about 3.6 us of
 * conversion at 72 MHz); keeps a stuck ADC from hanging the 10 ms task. */
#define ADC_TEMPERATURE_RETRY_LIMIT   (100000u)

volatile uint32_t adc_irq_count;

static adc_sample_callback_t s_callback;
static volatile uint16_t s_raw_current[3];
static volatile uint16_t s_raw_vbus;
static float s_current_offset[3];
static float s_current_scale;

static float adc_raw_to_amps(uint16_t raw, uint8_t phase)
{
  float volts = ((float)raw / CURRENT_SENSE_ADC_MAX) * CURRENT_SENSE_ADC_VREF_VOLT;
  return (CURRENT_SENSE_VREF_VOLT - volts + s_current_offset[phase]) / s_current_scale;
}

static void adc_read_preempt_group(void)
{
  s_raw_current[0] = adc_preempt_conversion_data_get(ADC1, ADC_PREEMPT_CHANNEL_1);
  s_raw_current[1] = adc_preempt_conversion_data_get(ADC1, ADC_PREEMPT_CHANNEL_2);
  s_raw_current[2] = adc_preempt_conversion_data_get(ADC1, ADC_PREEMPT_CHANNEL_3);
  s_raw_vbus = adc_preempt_conversion_data_get(ADC1, ADC_PREEMPT_CHANNEL_4);
}

void adc_init(void)
{
  adc_common_config_type common = {0};
  adc_base_config_type base = {0};
  crm_clocks_freq_type clocks = {0};

  crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
  crm_clocks_freq_get(&clocks);

  adc_reset();

  adc_common_default_para_init(&common);
  common.combine_mode = ADC_INDEPENDENT_MODE;
  common.div = ADC_HCLK_DIV_4;
  common.common_dma_mode = ADC_COMMON_DMAMODE_DISABLE;
  common.common_dma_request_repeat_state = FALSE;
  common.sampling_interval = ADC_SAMPLING_INTERVAL_5CYCLES;
  common.tempervintrv_state = FALSE;
  common.vbat_state = FALSE;
  adc_common_config(&common);

  adc_base_default_para_init(&base);
  base.sequence_mode = TRUE;
  base.repeat_mode = FALSE;
  base.data_align = ADC_RIGHT_ALIGNMENT;
  /* One ordinary (regular) channel: it is software triggered from the 10 ms
   * task to sample the temperature inputs.  Keeping them out of the injected
   * group keeps the 24 kHz current loop unchanged, and the injected group has
   * priority anyway, so a temperature conversion can never delay a current
   * sample. */
  base.ordinary_channel_length = 1;
  adc_base_config(ADC1, &base);
  adc_resolution_set(ADC1, ADC_RESOLUTION_12B);

  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_9, 1, ADC_SAMPLETIME_247_5);

  adc_preempt_channel_length_set(ADC1, 4);
  adc_preempt_channel_set(ADC1, ADC_CHANNEL_0, 1, ADC_SAMPLETIME_47_5);
  adc_preempt_channel_set(ADC1, ADC_CHANNEL_1, 2, ADC_SAMPLETIME_47_5);
  adc_preempt_channel_set(ADC1, ADC_CHANNEL_2, 3, ADC_SAMPLETIME_47_5);
  adc_preempt_channel_set(ADC1, ADC_CHANNEL_3, 4, ADC_SAMPLETIME_47_5);

  adc_preempt_conversion_trigger_set(ADC1, ADC_PREEMPT_TRIG_TMR1CH4, ADC_PREEMPT_TRIG_EDGE_RISING);

  adc_interrupt_enable(ADC1, ADC_PCCE_INT, TRUE);
  nvic_irq_enable(ADC1_2_3_IRQn, IRQ_PRIORITY_CURRENT_LOOP, 0);

  adc_enable(ADC1, TRUE);
  while (adc_flag_get(ADC1, ADC_RDY_FLAG) == RESET)
  {
  }
  adc_calibration_init(ADC1);
  while (adc_calibration_init_status_get(ADC1))
  {
  }
  adc_calibration_start(ADC1);
  while (adc_calibration_status_get(ADC1))
  {
  }

  s_current_scale = CURRENT_SENSE_GAIN * CURRENT_SENSE_SHUNT_OHM;
  for (uint8_t i = 0; i < 3; i++)
  {
    s_current_offset[i] = 0.0f;
  }
}

void adc_start(void)
{
  adc_preempt_conversion_trigger_set(ADC1, ADC_PREEMPT_TRIG_TMR1CH4, ADC_PREEMPT_TRIG_EDGE_RISING);
}

void adc_stop(void)
{
  adc_preempt_conversion_trigger_set(ADC1, ADC_PREEMPT_TRIG_TMR1CH4, ADC_PREEMPT_TRIG_EDGE_NONE);
}

void adc_register_sample_callback(adc_sample_callback_t callback)
{
  s_callback = callback;
}

bool adc_read_temperature_raw(adc_temp_input_t input, uint16_t *raw)
{
  adc_channel_select_type channel;
  uint32_t retry = 0;

  if (raw == NULL)
  {
    return false;
  }

  switch (input)
  {
    case ADC_TEMP_INPUT_1_PB0:
      channel = ADC_CHANNEL_8;
      break;

    case ADC_TEMP_INPUT_2_PB1:
      channel = ADC_CHANNEL_9;
      break;

    default:
      return false;
  }

  /*
   * The ordinary group holds a single channel which is re-ranked here, so one
   * software trigger always yields exactly one end-of-conversion flag.  That
   * keeps the read robust even when the injected current-loop group preempts in
   * the middle of it.
   */
  adc_flag_clear(ADC1, ADC_OCCE_FLAG);
  adc_ordinary_channel_set(ADC1, channel, 1, ADC_SAMPLETIME_247_5);
  adc_ordinary_software_trigger_enable(ADC1, TRUE);

  while (adc_flag_get(ADC1, ADC_OCCE_FLAG) == RESET)
  {
    if (++retry > ADC_TEMPERATURE_RETRY_LIMIT)
    {
      adc_ordinary_software_trigger_enable(ADC1, FALSE);
      return false;
    }
  }

  *raw = adc_ordinary_conversion_data_get(ADC1);
  adc_flag_clear(ADC1, ADC_OCCE_FLAG);
  adc_ordinary_software_trigger_enable(ADC1, FALSE);

  return true;
}

void adc_calibrate_current_offsets(void)
{
  const uint32_t samples = 256u;
  uint32_t accum[3] = {0u, 0u, 0u};

  adc_stop();
  adc_interrupt_enable(ADC1, ADC_PCCE_INT, FALSE);
  adc_flag_clear(ADC1, ADC_PCCE_FLAG);

  for (uint32_t i = 0; i < samples; i++)
  {
    adc_flag_clear(ADC1, ADC_PCCE_FLAG);
    adc_preempt_software_trigger_enable(ADC1, TRUE);
    while (adc_flag_get(ADC1, ADC_PCCE_FLAG) == RESET)
    {
    }
    adc_read_preempt_group();
    accum[0] += s_raw_current[0];
    accum[1] += s_raw_current[1];
    accum[2] += s_raw_current[2];
  }

  adc_preempt_software_trigger_enable(ADC1, FALSE);
  adc_interrupt_enable(ADC1, ADC_PCCE_INT, TRUE);
  adc_start();

  for (uint8_t i = 0; i < 3; i++)
  {
    float raw_average = (float)accum[i] / (float)samples;
    float volts = (raw_average / CURRENT_SENSE_ADC_MAX) * CURRENT_SENSE_ADC_VREF_VOLT;
    /* Offset is zero-current output voltage error relative to the ideal VREF. */
    s_current_offset[i] = CURRENT_SENSE_VREF_VOLT - volts;
  }
}

void ADC1_2_3_IRQHandler(void)
{
  if (adc_interrupt_flag_get(ADC1, ADC_PCCE_FLAG) != RESET)
  {
    float ia;
    float ib;
    float ic;
    float vbus;

    adc_flag_clear(ADC1, ADC_PCCE_FLAG);
    adc_read_preempt_group();
    adc_irq_count++;

    ia = adc_raw_to_amps(s_raw_current[0], 0);
    ib = adc_raw_to_amps(s_raw_current[1], 1);
    ic = adc_raw_to_amps(s_raw_current[2], 2);
    vbus = board_vbus_raw_to_volts(s_raw_vbus);

    if (s_callback != NULL)
    {
      s_callback(ia, ib, ic, vbus);
    }
  }
}
