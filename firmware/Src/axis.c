#include "axis.h"
#include "irq_priority.h"
#include "system_time.h"
#include "adc.h"
#include "board.h"
#include "foc.h"
#include "pwm.h"
#include "util.h"

#include <math.h>
#include <stddef.h>

axis_t g_axis;

static volatile bool s_calibration_active;
static uint32_t s_encoder_fault_streak;

void axis_apply_voltage_vector(float v_d, float v_q, float electrical_angle, float vbus)
{
  foc_dq_t v_dq;
  foc_alphabeta_t v_ab;
  float duty_a;
  float duty_b;
  float duty_c;
  float sin_theta = sinf(electrical_angle);
  float cos_theta = cosf(electrical_angle);

  v_dq.d = v_d;
  v_dq.q = v_q;
  foc_inverse_park(&v_dq, &v_ab, sin_theta, cos_theta);
  foc_svpwm(&v_ab, vbus, &duty_a, &duty_b, &duty_c);
  pwm_set_duty(duty_a, duty_b, duty_c);
}

static bool axis_bus_voltage_ok(const axis_t *axis)
{
  if (axis->vbus_voltage > axis->vbus_over_voltage)
  {
    return false;
  }
  if ((axis->vbus_voltage > 1.0f) && (axis->vbus_voltage < axis->vbus_under_voltage))
  {
    return false;
  }
  return true;
}

static void axis_protection_check(axis_t *axis)
{
  float max_current = fmaxf(fabsf(axis->phase_current_a),
                            fmaxf(fabsf(axis->phase_current_b), fabsf(axis->phase_current_c)));

  if (!axis_bus_voltage_ok(axis))
  {
    if (axis->vbus_voltage > axis->vbus_over_voltage)
    {
      axis_set_error(axis, AXIS_ERROR_DC_BUS_OVER_VOLTAGE);
    }
    else
    {
      axis_set_error(axis, AXIS_ERROR_DC_BUS_UNDER_VOLTAGE);
    }
    return;
  }

  if (max_current > axis->phase_current_limit + MOTOR_CURRENT_LIMIT_MARGIN_A)
  {
    axis_set_error(axis, AXIS_ERROR_CURRENT_LIMIT_VIOLATION);
    return;
  }

  if (axis->temp_mos > PROTECTION_TEMP_MAX_C)
  {
    axis_set_error(axis, AXIS_ERROR_MOTOR_OVER_TEMPERATURE);
    return;
  }
}

void axis_timer_init(void)
{
  crm_clocks_freq_type clocks = {0};
  uint32_t timer_clk;
  uint16_t period;

  crm_periph_clock_enable(CRM_TMR2_PERIPH_CLOCK, TRUE);
  crm_clocks_freq_get(&clocks);
  timer_clk = clocks.apb1_freq * 2u;
  period = (uint16_t)(timer_clk / BOARD_VELOCITY_LOOP_FREQ_HZ);
  if (period == 0u)
  {
    period = 1u;
  }

  tmr_base_init(TMR2, (uint32_t)(period - 1u), 0u);
  tmr_cnt_dir_set(TMR2, TMR_COUNT_UP);
  tmr_interrupt_enable(TMR2, TMR_OVF_INT, TRUE);
  nvic_irq_enable(TMR2_GLOBAL_IRQn, IRQ_PRIORITY_CONTROL_LOOP, 0);
  tmr_counter_enable(TMR2, TRUE);
}

void axis_init(axis_t *axis, encoder_type_t encoder_type)
{
  if (axis == NULL)
  {
    return;
  }

  s_encoder_fault_streak = 0u;

  axis->requested_state = AXIS_STATE_IDLE;
  axis->current_state = AXIS_STATE_IDLE;
  axis->last_state = AXIS_STATE_UNDEFINED;
  axis->error = AXIS_ERROR_NONE;
  axis->armed = false;
  axis->calibration_ok = false;
  axis->encoder_offset_valid = false;
  axis->motor_calibrated = false;
  axis->calibration_busy = false;
  /*
   * Values that are persisted in nvm_config are published by
   * nvm_config_apply(), so their defaults live in nvm_config_defaults() only.
   * Zero them here instead of repeating the defaults (app_init() calls
   * nvm_config_apply() before the control interrupts are started).
   */
  axis->phase_resistance = 0.0f;
  axis->phase_inductance = 0.0f;
  axis->motor_torque_constant = 0.0f;
  axis->motor_flux_linkage = 0.0f;
  axis->r_wl_ff_enable = false;
  axis->bemf_ff_enable = false;
  axis->vbus_voltage = 0.0f;
  axis->phase_current_a = 0.0f;
  axis->phase_current_b = 0.0f;
  axis->phase_current_c = 0.0f;
  axis->i_d_measured = 0.0f;
  axis->i_q_measured = 0.0f;
  axis->i_d_setpoint = 0.0f;
  axis->i_q_setpoint = 0.0f;
  axis->v_d_setpoint = 0.0f;
  axis->v_q_setpoint = 0.0f;
  axis->pos_estimate = 0.0f;
  axis->vel_estimate = 0.0f;
  axis->electrical_angle = 0.0f;
  axis->phase_current_limit = 0.0f;
  axis->dc_bus_power_limit = CONTROLLER_DC_BUS_POWER_LIMIT_W;
  axis->dc_bus_regen_limit = CONTROLLER_DC_BUS_REGEN_LIMIT_W;
  axis->dc_bus_current_max = PROTECTION_DC_BUS_CURRENT_MAX_A;
  axis->i_bus_estimate = 0.0f;
  axis->power_estimate = 0.0f;
  axis->duty_a = 0.5f;
  axis->duty_b = 0.5f;
  axis->duty_c = 0.5f;
  axis->vbus_over_voltage = PROTECTION_BUS_OVER_VOLTAGE_V;
  axis->vbus_under_voltage = PROTECTION_BUS_UNDER_VOLTAGE_V;
  /* Replaced by real measurements once axis_update_temperatures() runs. */
  axis->temp_mos = 25.0f;
  axis->temp_motor = 25.0f;
  axis->temp_mos_raw = 0u;
  axis->temp_motor_raw = 0u;
  axis->control_loop_count = 0u;
  axis->slow_loop_count = 0u;
  axis->last_error = 0u;
  axis->last_error_time_ms = 0u;
  axis->last_communication_ms = system_millis();
  axis->communication_watchdog_timeout_ms = 0u;
  axis->communication_watchdog_enabled = false;
  axis->can_node_id = CAN_NODE_ID_DEFAULT;

  controller_init(&axis->controller);
  controller_set_current_limits(&axis->controller, axis->phase_current_limit);
  encoder_init(&axis->encoder, encoder_type, ENCODER_MT6816_CPR, ENCODER_DIRECTION_DEFAULT);
}

void axis_set_requested_state(axis_t *axis, axis_state_t state)
{
  if (axis == NULL)
  {
    return;
  }

  if (state == AXIS_STATE_IDLE)
  {
    axis->error = AXIS_ERROR_NONE;
  }
  axis->requested_state = state;
}

void axis_set_error(axis_t *axis, uint32_t error)
{
  if (axis == NULL)
  {
    return;
  }

  /*
   * axis_set_error() is called from the 24 kHz current loop, the 8 kHz control
   * loop and the main loop.  Guard the read-modify-write so a fault reported
   * from one context cannot be lost to a concurrent one.
   */
  {
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    axis->error |= error;
    if (primask == 0u)
    {
      __enable_irq();
    }
  }

  axis->last_error = error;
  axis->last_error_time_ms = system_millis();
  axis->armed = false;
  axis->current_state = AXIS_STATE_ERROR;
  axis->requested_state = AXIS_STATE_ERROR;
  pwm_disable();
}

void axis_set_calibration_active(bool active)
{
  s_calibration_active = active;
}

void axis_arm(axis_t *axis)
{
  if ((axis == NULL) || (axis->error != AXIS_ERROR_NONE))
  {
    return;
  }

  if (!axis_bus_voltage_ok(axis))
  {
    axis_set_error(axis, AXIS_ERROR_DC_BUS_UNDER_VOLTAGE);
    return;
  }

  axis->armed = true;
}

void axis_disarm(axis_t *axis)
{
  if (axis != NULL)
  {
    axis->armed = false;
    pwm_set_zero_vector();
  }
}

bool axis_set_controller_mode(axis_t *axis, controller_mode_t control_mode, input_mode_t input_mode)
{
  if (axis == NULL)
  {
    return false;
  }
  controller_set_control_mode(&axis->controller, control_mode, input_mode);
  return true;
}

void axis_set_input_pos(axis_t *axis, float pos)
{
  if (axis != NULL)
  {
    controller_set_input_pos(&axis->controller, pos);
  }
}

void axis_set_input_vel(axis_t *axis, float vel)
{
  if (axis != NULL)
  {
    controller_set_input_vel(&axis->controller, vel);
  }
}

void axis_set_input_torque(axis_t *axis, float torque)
{
  if (axis != NULL)
  {
    controller_set_input_torque(&axis->controller, torque);
  }
}

void axis_set_limits(axis_t *axis, float current_limit, float vel_limit)
{
  if (axis == NULL)
  {
    return;
  }

  axis->phase_current_limit = util_clampf(current_limit, 0.0f, MOTOR_CURRENT_LIMIT_A);
  axis->controller.vel_limit = util_clampf(vel_limit, 0.0f, CONTROLLER_VEL_LIMIT_REV_S);
  controller_set_current_limits(&axis->controller, axis->phase_current_limit);
}

void axis_note_communication(axis_t *axis)
{
  if (axis != NULL)
  {
    axis->last_communication_ms = system_millis();
  }
}

void axis_communication_watchdog_update(axis_t *axis, uint32_t now_ms)
{
  if ((axis == NULL) || (!axis->communication_watchdog_enabled))
  {
    return;
  }

  if (axis->communication_watchdog_timeout_ms == 0u)
  {
    return;
  }

  if ((now_ms - axis->last_communication_ms) > axis->communication_watchdog_timeout_ms)
  {
    axis_set_error(axis, AXIS_ERROR_WATCHDOG_TIMER_EXPIRED);
  }
}

void axis_current_loop_callback(float ia, float ib, float ic, float vbus)
{
  axis_t *axis = &g_axis;
  foc_abc_t i_abc;
  foc_alphabeta_t i_ab;
  foc_dq_t i_dq;
  foc_dq_t v_dq;
  foc_alphabeta_t v_ab;
  float sin_theta;
  float cos_theta;
  float duty_a;
  float duty_b;
  float duty_c;
  float dt = 1.0f / (float)BOARD_CURRENT_LOOP_FREQ_HZ;

  axis->control_loop_count++;
  axis->phase_current_a = ia;
  axis->phase_current_b = ib;
  axis->phase_current_c = ic;
  axis->vbus_voltage = vbus;

  if ((!axis->armed) || (axis->error != AXIS_ERROR_NONE) || s_calibration_active)
  {
    return;
  }

  axis_protection_check(axis);
  if (axis->error != AXIS_ERROR_NONE)
  {
    return;
  }

  if (fabsf(axis->i_bus_estimate) > axis->dc_bus_current_max)
  {
    axis_set_error(axis, AXIS_ERROR_DC_BUS_OVER_CURRENT);
    return;
  }

  sin_theta = sinf(axis->electrical_angle);
  cos_theta = cosf(axis->electrical_angle);

  i_abc.a = ia;
  i_abc.b = ib;
  i_abc.c = ic;
  foc_clarke(&i_abc, &i_ab);
  foc_park(&i_ab, &i_dq, sin_theta, cos_theta);

  axis->i_d_measured = i_dq.d;
  axis->i_q_measured = i_dq.q;

  /*
   * Simplified DC bus power limiting.  Use the previous-cycle power estimate
   * to reduce the d/q current vector when motoring or regenerating beyond the
   * configured limits.  This board has no brake chopper, so regen limit is
   * intentionally conservative.
   */
  {
    float dynamic_limit = axis->phase_current_limit;
    float vector_mag = sqrtf((axis->controller.i_d_setpoint * axis->controller.i_d_setpoint) +
                             (axis->controller.i_q_setpoint * axis->controller.i_q_setpoint));

    if (axis->vbus_voltage > 1.0f)
    {
      if (axis->power_estimate > axis->dc_bus_power_limit)
      {
        dynamic_limit = axis->dc_bus_power_limit / axis->vbus_voltage;
      }
      else if (axis->power_estimate < -axis->dc_bus_regen_limit)
      {
        dynamic_limit = axis->dc_bus_regen_limit / axis->vbus_voltage;
      }
    }

    if ((vector_mag > dynamic_limit) && (vector_mag > 1.0e-6f))
    {
      float scale = dynamic_limit / vector_mag;
      axis->controller.i_d_setpoint *= scale;
      axis->controller.i_q_setpoint *= scale;
    }
  }

  controller_update_current_references(&axis->controller, axis->i_d_measured, axis->i_q_measured, dt);
  axis->i_d_setpoint = axis->controller.i_d_setpoint;
  axis->i_q_setpoint = axis->controller.i_q_setpoint;

  /*
   * Current-loop feed-forward and d/q decoupling:
   *   v_d_ff = R*Id - w_e*L*Iq
   *   v_q_ff = R*Iq + w_e*L*Id + w_e*psi_f
   */
  {
    float pole_pairs = (float)axis->encoder.pole_pairs;
    float omega_e = axis->vel_estimate * pole_pairs * 2.0f * UTIL_PI_F;
    float v_d_ff = 0.0f;
    float v_q_ff = 0.0f;
    float v_mag;
    float v_max;

    if (axis->r_wl_ff_enable && (axis->phase_resistance > 0.0f) && (axis->phase_inductance > 0.0f))
    {
      v_d_ff += (axis->phase_resistance * axis->i_d_setpoint) -
                (omega_e * axis->phase_inductance * axis->i_q_setpoint);
      v_q_ff += (axis->phase_resistance * axis->i_q_setpoint) +
                (omega_e * axis->phase_inductance * axis->i_d_setpoint);
    }

    if (axis->bemf_ff_enable && (axis->motor_flux_linkage > 0.0f))
    {
      v_q_ff += omega_e * axis->motor_flux_linkage;
    }

    axis->controller.v_d_setpoint += v_d_ff;
    axis->controller.v_q_setpoint += v_q_ff;

    /* Limit the voltage vector to the linear SVPWM range. */
    v_mag = sqrtf((axis->controller.v_d_setpoint * axis->controller.v_d_setpoint) +
                  (axis->controller.v_q_setpoint * axis->controller.v_q_setpoint));
    v_max = (vbus > 1.0f) ? (vbus * UTIL_INV_SQRT3_F) : 1.0f;
    if ((v_mag > v_max) && (v_mag > 1.0e-6f))
    {
      float scale = v_max / v_mag;
      axis->controller.v_d_setpoint *= scale;
      axis->controller.v_q_setpoint *= scale;
    }
  }

  axis->v_d_setpoint = axis->controller.v_d_setpoint;
  axis->v_q_setpoint = axis->controller.v_q_setpoint;

  v_dq.d = axis->v_d_setpoint;
  v_dq.q = axis->v_q_setpoint;
  foc_inverse_park(&v_dq, &v_ab, sin_theta, cos_theta);
  foc_svpwm(&v_ab, vbus, &duty_a, &duty_b, &duty_c);
  pwm_set_duty(duty_a, duty_b, duty_c);

  axis->duty_a = duty_a;
  axis->duty_b = duty_b;
  axis->duty_c = duty_c;
  axis->i_bus_estimate = (duty_a * ia) + (duty_b * ib) + (duty_c * ic);
  axis->power_estimate = axis->vbus_voltage * axis->i_bus_estimate;
}

void axis_update_temperatures(axis_t *axis)
{
  static uint32_t s_sample_tick;
  static uint32_t s_fet_sensor_fault_streak;
  uint16_t raw;

  if (axis == NULL)
  {
    return;
  }

  /* Thermal time constants are seconds; 100 Hz is far more than enough and it
   * keeps the 1 ms task free of per-tick conversion overhead. */
  if (++s_sample_tick < TEMP_SAMPLE_PERIOD_MS)
  {
    return;
  }
  s_sample_tick = 0u;

  /*
   * TEMP_2 / PB1 is the on-board NTC next to the power stage; it is the input
   * for the over-temperature trip in axis_protection_check().  Either a failed
   * conversion or an implausible reading (open, shorted, not fitted) silently
   * removes that protection, so both count towards the same fault streak.
   * It is latched only while the axis is armed, so a board without the NTC can
   * still be calibrated on the bench.
   */
  {
    bool fetched = adc_read_temperature_raw(ADC_TEMP_INPUT_2_PB1, &raw);
    bool plausible = fetched &&
                     (raw >= TEMP_NTC_RAW_MIN_COUNTS) &&
                     (raw <= TEMP_NTC_RAW_MAX_COUNTS);

    if (fetched)
    {
      /* The raw counts stay visible for bring-up even when implausible. */
      axis->temp_mos_raw = raw;
    }

    if (plausible)
    {
      axis->temp_mos = board_temp_raw_to_celsius(raw);
      s_fet_sensor_fault_streak = 0u;
    }
    else
    {
      /* Keep the last plausible value rather than publishing a bogus number. */
      if (axis->armed && (++s_fet_sensor_fault_streak >= TEMP_SENSOR_FAULT_STREAK_LIMIT))
      {
        s_fet_sensor_fault_streak = 0u;
        axis_set_error(axis, AXIS_ERROR_TEMPERATURE_SENSOR_FAILED);
      }
    }
  }

  /*
   * TEMP_1 / PB0 is the optional off-board sensor (motor thermistor).  It has
   * no over-temperature authority because it may legitimately be unconnected,
   * so the reading is published for monitoring only.
   */
  if (adc_read_temperature_raw(ADC_TEMP_INPUT_1_PB0, &raw))
  {
    axis->temp_motor_raw = raw;
    axis->temp_motor = board_temp_raw_to_celsius(raw);
  }
}

void axis_slow_loop(axis_t *axis, float dt)
{
  if (axis == NULL)
  {
    return;
  }

  if (axis->calibration_busy)
  {
    return;
  }

  axis->slow_loop_count++;

  /*
   * Read the absolute encoder at the slow-loop rate.
   *
   * While the axis is armed (closed loop) a failed sample must not leave the
   * drive running on a stale position/electrical angle, so a short run of
   * consecutive failures latches AXIS_ERROR_ENCODER_FAILED and drops PWM.
   * The streak debounces single-bit SPI glitches; the limit is in config.h.
   *
   * When the axis is not armed nothing is being driven from the encoder, so the
   * failure is not latched: motor R/L calibration must stay usable even when the
   * encoder is missing or broken.  The check re-arms by itself as soon as
   * CLOSED_LOOP_CONTROL sets `armed`.
   */
  if (encoder_update(&axis->encoder, dt))
  {
    s_encoder_fault_streak = 0u;
  }
  else if (!axis->armed)
  {
    s_encoder_fault_streak = 0u;
  }
  else if (++s_encoder_fault_streak >= ENCODER_FAULT_STREAK_LIMIT)
  {
    s_encoder_fault_streak = 0u;
    axis_set_error(axis, AXIS_ERROR_ENCODER_FAILED);
    return;
  }

  axis->pos_estimate = axis->encoder.pos_estimate;
  axis->vel_estimate = axis->encoder.vel_estimate;
  axis->electrical_angle = encoder_get_electrical_angle(&axis->encoder, (float)axis->encoder.pole_pairs);

  if ((axis->error == AXIS_ERROR_NONE) && (axis->current_state != AXIS_STATE_ERROR))
  {
    controller_update(&axis->controller, axis->pos_estimate, axis->vel_estimate, dt);
  }

  if ((axis->error != AXIS_ERROR_NONE) && (axis->requested_state != AXIS_STATE_IDLE))
  {
    axis->current_state = AXIS_STATE_ERROR;
    axis_disarm(axis);
    pwm_disable();
    return;
  }

  if ((axis->requested_state != axis->current_state) &&
      (axis->requested_state != AXIS_STATE_UNDEFINED))
  {
    axis->last_state = axis->current_state;
    axis->current_state = axis->requested_state;

    switch (axis->current_state)
    {
      case AXIS_STATE_IDLE:
        axis_disarm(axis);
        pwm_disable();
        break;

      case AXIS_STATE_ENCODER_OFFSET_CALIBRATION:
      case AXIS_STATE_FULL_CALIBRATION_SEQUENCE:
      case AXIS_STATE_MOTOR_CALIBRATION:
        /*
         * Blocking calibration is executed from the main loop so the
         * TMR2/ADC interrupts can keep running.  Mark the axis busy and
         * leave PWM control to the calibration module.
         */
        axis->calibration_busy = true;
        axis->current_state = axis->requested_state;
        break;

      case AXIS_STATE_CLOSED_LOOP_CONTROL:
        if (!axis->encoder_offset_valid)
        {
          axis_set_error(axis, AXIS_ERROR_INVALID_STATE);
        }
        else
        {
          axis_arm(axis);
          if (axis->armed)
          {
            pwm_enable();
          }
        }
        break;

      default:
        break;
    }
  }

  if (((axis->current_state == AXIS_STATE_CLOSED_LOOP_CONTROL) && axis->armed) ||
      (axis->current_state == AXIS_STATE_LOCKIN_SPIN))
  {
    /* Active state: keep the PWM running. */
    if (!pwm_is_enabled())
    {
      pwm_enable();
    }
  }
  else
  {
    pwm_disable();
    controller_reset(&axis->controller);
  }
}

void TMR2_GLOBAL_IRQHandler(void)
{
  if (tmr_flag_get(TMR2, TMR_OVF_FLAG) != RESET)
  {
    tmr_flag_clear(TMR2, TMR_OVF_FLAG);
    axis_slow_loop(&g_axis, 1.0f / (float)BOARD_VELOCITY_LOOP_FREQ_HZ);
  }
}
