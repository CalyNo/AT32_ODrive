#ifndef AT32_ODRIVE_ENCODER_PLL_H
#define AT32_ODRIVE_ENCODER_PLL_H

#include <stdbool.h>
#include <stdint.h>

#include "encoder.h"

/*
 * Pure estimator part of the encoder module.
 *
 * encoder_pll.c contains no hardware access, so the position/velocity PLL and
 * the electrical angle computation can be exercised by the host tests in
 * tests/.  encoder.c owns the SPI transport and feeds raw counts into
 * encoder_pll_update().
 */

/* Default PLL bandwidth used until nvm_config publishes the stored value. */
#define ENCODER_PLL_DEFAULT_BANDWIDTH_HZ   (1000.0f)

/*
 * Initialise every estimator field of an encoder instance.  encoder_init() in
 * encoder.c calls this before it touches the SPI peripheral, which keeps the
 * two halves of the module in step and lets the host tests build the exact same
 * starting state.
 */
void encoder_pll_init(encoder_t *enc, encoder_type_t type, float cpr, float direction,
                      int32_t pole_pairs, float bandwidth_hz);

/*
 * Consume one absolute count sample (already validated by the caller) and
 * advance the estimates.  Returns false when the sample cannot be used, in
 * which case pos_estimate_valid is cleared and error_count is incremented.
 */
bool encoder_pll_update(encoder_t *enc, uint16_t raw, float dt);

#endif /* AT32_ODRIVE_ENCODER_PLL_H */
