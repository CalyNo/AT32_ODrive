#ifndef AT32_ODRIVE_AXIS_H
#define AT32_ODRIVE_AXIS_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "controller.h"
#include "encoder.h"

typedef enum
{
  AXIS_STATE_UNDEFINED = 0,
  AXIS_STATE_IDLE = 1,
  AXIS_STATE_STARTUP_SEQUENCE = 2,
  AXIS_STATE_FULL_CALIBRATION_SEQUENCE = 3,
  AXIS_STATE_MOTOR_CALIBRATION = 4,
  AXIS_STATE_SENSORLESS_CONTROL = 5,
  AXIS_STATE_ENCODER_INDEX_SEARCH = 6,
  AXIS_STATE_ENCODER_OFFSET_CALIBRATION = 7,
  AXIS_STATE_CLOSED_LOOP_CONTROL = 8,
  AXIS_STATE_LOCKIN_SPIN = 9,
  AXIS_STATE_ENCODER_DIR_FIND = 10,
  AXIS_STATE_HOMING = 11,
  AXIS_STATE_ENCODER_HALL_POLARITY_CALIBRATION = 12,
  AXIS_STATE_ENCODER_HALL_PHASE_CALIBRATION = 13,
  AXIS_STATE_ANTICOGGING_CALIBRATION = 14,
  AXIS_STATE_ERROR = 15
} axis_state_t;

typedef enum
{
  AXIS_ERROR_NONE = 0,
  AXIS_ERROR_INVALID_STATE = (1u << 0),
  AXIS_ERROR_DC_BUS_OVER_VOLTAGE = (1u << 1),
  AXIS_ERROR_DC_BUS_UNDER_VOLTAGE = (1u << 2),
  AXIS_ERROR_DC_BUS_OVER_CURRENT = (1u << 3),
  AXIS_ERROR_CURRENT_LIMIT_VIOLATION = (1u << 4),
  AXIS_ERROR_MOTOR_OVER_TEMPERATURE = (1u << 5),
  AXIS_ERROR_ENCODER_FAILED = (1u << 6),
  AXIS_ERROR_CONTROLLER_FAILED = (1u << 7),
  AXIS_ERROR_POSITION_LIMIT_VIOLATION = (1u << 8),
  AXIS_ERROR_WATCHDOG_TIMER_EXPIRED = (1u << 9),
  AXIS_ERROR_CALIBRATION_FAILED = (1u << 10),
  AXIS_ERROR_ESTOP_REQUESTED = (1u << 11),
  AXIS_ERROR_TEMPERATURE_SENSOR_FAILED = (1u << 12)
} axis_error_t;

/*
 * Error categories used by the CAN Simple heartbeat flags and by the per-block
 * "*_error" queries.  CAN hosts read these as independent booleans, so the bits
 * have to be grouped the same way ODrive groups them.
 */
#define AXIS_ERROR_MASK_MOTOR      (AXIS_ERROR_DC_BUS_OVER_VOLTAGE | \
                                    AXIS_ERROR_DC_BUS_UNDER_VOLTAGE | \
                                    AXIS_ERROR_DC_BUS_OVER_CURRENT | \
                                    AXIS_ERROR_CURRENT_LIMIT_VIOLATION | \
                                    AXIS_ERROR_MOTOR_OVER_TEMPERATURE | \
                                    AXIS_ERROR_TEMPERATURE_SENSOR_FAILED)

#define AXIS_ERROR_MASK_ENCODER    (AXIS_ERROR_ENCODER_FAILED)

#define AXIS_ERROR_MASK_CONTROLLER (AXIS_ERROR_CONTROLLER_FAILED | \
                                    AXIS_ERROR_POSITION_LIMIT_VIOLATION | \
                                    AXIS_ERROR_INVALID_STATE)

typedef struct
{
  axis_state_t requested_state;
  axis_state_t current_state;
  axis_state_t last_state;
  uint32_t error;

  bool armed;
  bool calibration_ok;
  bool encoder_offset_valid;
  bool motor_calibrated;
  volatile bool calibration_busy;

  float phase_resistance;
  float phase_inductance;
  float motor_torque_constant;
  float motor_flux_linkage;
  bool r_wl_ff_enable;
  bool bemf_ff_enable;

  float vbus_voltage;
  float phase_current_a;
  float phase_current_b;
  float phase_current_c;
  float i_d_measured;
  float i_q_measured;
  float i_d_setpoint;
  float i_q_setpoint;
  float v_d_setpoint;
  float v_q_setpoint;
  float pos_estimate;
  float vel_estimate;
  float electrical_angle;

  float phase_current_limit;
  float dc_bus_power_limit;
  float dc_bus_regen_limit;
  float dc_bus_current_max;
  float i_bus_estimate;
  float power_estimate;
  float duty_a;
  float duty_b;
  float duty_c;
  float vbus_over_voltage;
  float vbus_under_voltage;
  float temp_mos;
  float temp_motor;
  uint16_t temp_mos_raw;      /* TEMP_2 / PB1 raw ADC counts */
  uint16_t temp_motor_raw;    /* TEMP_1 / PB0 raw ADC counts */

  uint32_t control_loop_count;
  uint32_t slow_loop_count;

  uint32_t last_error;
  uint32_t last_error_time_ms;
  uint32_t last_communication_ms;
  uint32_t communication_watchdog_timeout_ms;
  bool communication_watchdog_enabled;
  uint32_t can_node_id;

  controller_t controller;
  encoder_t encoder;
} axis_t;

extern axis_t g_axis;

void axis_init(axis_t *axis, encoder_type_t encoder_type);
void axis_timer_init(void);
void axis_set_requested_state(axis_t *axis, axis_state_t state);
void axis_set_error(axis_t *axis, uint32_t error);
void axis_set_calibration_active(bool active);
void axis_apply_voltage_vector(float v_d, float v_q, float electrical_angle, float vbus);
void axis_arm(axis_t *axis);
void axis_disarm(axis_t *axis);
bool axis_set_controller_mode(axis_t *axis, controller_mode_t control_mode, input_mode_t input_mode);
void axis_set_input_pos(axis_t *axis, float pos);
void axis_set_input_vel(axis_t *axis, float vel);
void axis_set_input_torque(axis_t *axis, float torque);
void axis_set_limits(axis_t *axis, float current_limit, float vel_limit);
void axis_note_communication(axis_t *axis);
void axis_communication_watchdog_update(axis_t *axis, uint32_t now_ms);
void axis_current_loop_callback(float ia, float ib, float ic, float vbus);
void axis_slow_loop(axis_t *axis, float dt);
/* Sample both temperature inputs (PB0 = TEMP_1, PB1 = TEMP_2) and update
 * temp_mos / temp_motor.  Called from the 1 ms system task; internally
 * rate-limited to TEMP_SAMPLE_PERIOD_MS. */
void axis_update_temperatures(axis_t *axis);

#endif /* AT32_ODRIVE_AXIS_H */
