#ifndef AT32_ODRIVE_ADC_H
#define AT32_ODRIVE_ADC_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Temperature inputs (see docs/pinout.md):
 *   TEMP_1 = PB0 = ADC1_IN8  (external / off-board sensor)
 *   TEMP_2 = PB1 = ADC1_IN9  (on-board NTC, used for the FET over-temperature trip)
 */
typedef enum
{
  ADC_TEMP_INPUT_1_PB0 = 0,
  ADC_TEMP_INPUT_2_PB1 = 1
} adc_temp_input_t;

void adc_init(void);
void adc_start(void);
void adc_stop(void);

/* Callback invoked from the ADC injected conversion interrupt. */
typedef void (*adc_sample_callback_t)(float ia, float ib, float ic, float vbus);
void adc_register_sample_callback(adc_sample_callback_t callback);

void adc_calibrate_current_offsets(void);

/* Sample one temperature input through the ordinary group.  Returns false when
 * the conversion did not complete. */
bool adc_read_temperature_raw(adc_temp_input_t input, uint16_t *raw);

extern volatile uint32_t adc_irq_count;

#endif /* AT32_ODRIVE_ADC_H */
