#ifndef AT32_ODRIVE_CALIBRATION_H
#define AT32_ODRIVE_CALIBRATION_H

#include <stdbool.h>
#include "axis.h"

/*
 * Blocking calibration helpers.
 *
 * These functions are called from the main loop after the axis state machine
 * has set calibration_busy.  They feed the independent watchdog internally.
 */
bool calibration_process(axis_t *axis);

bool calibration_motor_rl(axis_t *axis);
bool calibration_encoder_offset(axis_t *axis);

#endif /* AT32_ODRIVE_CALIBRATION_H */
