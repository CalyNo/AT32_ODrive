#include "can_comm.h"
#include "irq_priority.h"
#include "system_time.h"
#include "axis.h"
#include "board.h"
#include "config.h"
#include "nvm_config.h"
#include "param.h"

#include <stddef.h>
#include <string.h>

/*
 * ODrive CAN Simple command IDs.
 * Layout: (node_id << 5) | cmd, standard 11-bit ID.
 */
#define CAN_SIMPLE_MSG_CO_NMT_CTRL              0x00u
#define CAN_SIMPLE_MSG_ODRIVE_HEARTBEAT         0x01u
#define CAN_SIMPLE_MSG_ODRIVE_ESTOP             0x02u
#define CAN_SIMPLE_MSG_GET_MOTOR_ERROR          0x03u
#define CAN_SIMPLE_MSG_GET_ENCODER_ERROR        0x04u
#define CAN_SIMPLE_MSG_GET_SENSORLESS_ERROR     0x05u
#define CAN_SIMPLE_MSG_SET_AXIS_NODE_ID         0x06u
#define CAN_SIMPLE_MSG_SET_AXIS_REQUESTED_STATE 0x07u
#define CAN_SIMPLE_MSG_SET_AXIS_STARTUP_CONFIG  0x08u
#define CAN_SIMPLE_MSG_GET_ENCODER_ESTIMATES    0x09u
#define CAN_SIMPLE_MSG_GET_ENCODER_COUNT        0x0Au
#define CAN_SIMPLE_MSG_SET_CONTROLLER_MODES     0x0Bu
#define CAN_SIMPLE_MSG_SET_INPUT_POS            0x0Cu
#define CAN_SIMPLE_MSG_SET_INPUT_VEL            0x0Du
#define CAN_SIMPLE_MSG_SET_INPUT_TORQUE         0x0Eu
#define CAN_SIMPLE_MSG_SET_LIMITS               0x0Fu
#define CAN_SIMPLE_MSG_START_ANTICOGGING        0x10u
#define CAN_SIMPLE_MSG_SET_TRAJ_VEL_LIMIT       0x11u
#define CAN_SIMPLE_MSG_SET_TRAJ_ACCEL_LIMITS    0x12u
#define CAN_SIMPLE_MSG_SET_TRAJ_INERTIA         0x13u
#define CAN_SIMPLE_MSG_GET_IQ                   0x14u
#define CAN_SIMPLE_MSG_GET_SENSORLESS_ESTIMATES 0x15u
#define CAN_SIMPLE_MSG_RESET_ODRIVE             0x16u
#define CAN_SIMPLE_MSG_GET_BUS_VOLTAGE_CURRENT  0x17u
#define CAN_SIMPLE_MSG_CLEAR_ERRORS             0x18u
#define CAN_SIMPLE_MSG_SET_LINEAR_COUNT         0x19u
#define CAN_SIMPLE_MSG_SET_POS_GAIN             0x1Au
#define CAN_SIMPLE_MSG_SET_VEL_GAINS            0x1Bu
#define CAN_SIMPLE_MSG_GET_ADC_VOLTAGE          0x1Cu
#define CAN_SIMPLE_MSG_GET_CONTROLLER_ERROR     0x1Du
#define CAN_SIMPLE_MSG_CO_HEARTBEAT_CMD         0x700u

#define CAN_ENCODER_RATE_MS      (10u)
#define CAN_BUS_VI_RATE_MS       (100u)

#define CAN_RX_QUEUE_SIZE        (8u)

typedef struct
{
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} can_rx_item_t;

static volatile can_rx_item_t s_rx_queue[CAN_RX_QUEUE_SIZE];
static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;

static uint32_t s_last_heartbeat_ms;
static uint32_t s_last_encoder_ms;
static uint32_t s_last_bus_vi_ms;

static uint32_t can_simple_id(uint8_t cmd)
{
  return ((uint32_t)g_axis.can_node_id << 5) | (uint32_t)cmd;
}

static float can_read_float_le(const uint8_t *data)
{
  float value;
  memcpy(&value, data, sizeof(float));
  return value;
}

static int32_t can_read_i32_le(const uint8_t *data)
{
  return (int32_t)((uint32_t)data[0] |
                   ((uint32_t)data[1] << 8) |
                   ((uint32_t)data[2] << 16) |
                   ((uint32_t)data[3] << 24));
}

static void can_rx_push(const can_rx_message_type *msg)
{
  uint8_t next = (uint8_t)((s_rx_head + 1u) % CAN_RX_QUEUE_SIZE);

  if (next == s_rx_tail)
  {
    return;
  }

  s_rx_queue[s_rx_head].id = msg->standard_id;
  s_rx_queue[s_rx_head].dlc = (uint8_t)msg->dlc;
  memcpy((void *)s_rx_queue[s_rx_head].data, msg->data, 8u);
  s_rx_head = next;
}

static bool can_rx_pop(can_rx_item_t *item)
{
  if (s_rx_tail == s_rx_head)
  {
    return false;
  }

  item->id = s_rx_queue[s_rx_tail].id;
  item->dlc = s_rx_queue[s_rx_tail].dlc;
  memcpy(item->data, (const void *)s_rx_queue[s_rx_tail].data, 8u);
  s_rx_tail = (uint8_t)((s_rx_tail + 1u) % CAN_RX_QUEUE_SIZE);
  return true;
}

static void can_send(uint32_t id, const uint8_t *data, uint8_t dlc)
{
  can_tx_message_type tx = {0};
  uint8_t mailbox;

  tx.standard_id = (uint16_t)id;
  tx.extended_id = 0u;
  tx.id_type = CAN_ID_STANDARD;
  tx.frame_type = CAN_TFT_DATA;
  tx.dlc = dlc;
  for (uint8_t i = 0; i < dlc && i < 8u; i++)
  {
    tx.data[i] = data[i];
  }

  mailbox = can_message_transmit(CAN1, &tx);
  (void)mailbox;
}

static void can_send_u32(uint8_t cmd, uint32_t value)
{
  uint8_t data[4];
  data[0] = (uint8_t)(value & 0xFFu);
  data[1] = (uint8_t)((value >> 8) & 0xFFu);
  data[2] = (uint8_t)((value >> 16) & 0xFFu);
  data[3] = (uint8_t)((value >> 24) & 0xFFu);
  can_send(can_simple_id(cmd), data, 4u);
}

static void can_send_f32(uint8_t cmd, float value)
{
  uint8_t data[4];
  memcpy(data, &value, sizeof(float));
  can_send(can_simple_id(cmd), data, 4u);
}

static void can_send_float_pair(uint8_t cmd, float first, float second)
{
  uint8_t data[8];
  memcpy(data, &first, sizeof(float));
  memcpy(data + 4, &second, sizeof(float));
  can_send(can_simple_id(cmd), data, 8u);
}

/*
 * CAN bit timing.
 *
 * The Artery library defines: baudrate = fpclk / (baudrate_div * (3 + bts1 + bts2))
 * where bts1/bts2 are the register values.  BS1 = 13 TQ and BS2 = 2 TQ keep the
 * sample point at the CAN-recommended 87.5% and make the sum exactly 16, which
 * divides cleanly for every standard bit rate (1M/500k/250k/125k/100k).
 */
#define CAN_BIT_TQ_SUM   (3u + 12u + 1u)

static bool can_comm_baudrate_apply(uint32_t baudrate)
{
  crm_clocks_freq_type clocks = {0};
  can_baudrate_type baud = {0};
  uint32_t div;

  if (baudrate == 0u)
  {
    return false;
  }

  crm_clocks_freq_get(&clocks);
  div = clocks.apb1_freq / (baudrate * CAN_BIT_TQ_SUM);
  if ((div == 0u) || (div > 0x1000u))
  {
    return false;
  }

  baud.baudrate_div = (uint16_t)div;
  baud.rsaw_size = CAN_RSAW_1TQ;
  baud.bts1_size = CAN_BTS1_13TQ;
  baud.bts2_size = CAN_BTS2_2TQ;

  return can_baudrate_set(CAN1, &baud) == SUCCESS;
}

void can_comm_init(void)
{
  can_base_type can_base = {0};
  can_filter_init_type filter = {0};

  crm_periph_clock_enable(CRM_CAN1_PERIPH_CLOCK, TRUE);

  can_default_para_init(&can_base);
  can_base.mode_selection = CAN_MODE_COMMUNICATE;
  can_base.ttc_enable = FALSE;
  can_base.aebo_enable = TRUE;
  can_base.aed_enable = TRUE;
  can_base.prsf_enable = FALSE;
  can_base.mdrsel_selection = CAN_DISCARDING_FIRST_RECEIVED;
  can_base.mmssr_selection = CAN_SENDING_BY_ID;
  can_base_init(CAN1, &can_base);

  /* Configured bit rate (see can.config.baud_rate).  Invalid or unachievable
   * values fall back to the compile-time default rather than silently running
   * at an arbitrary rate. */
  if (!can_comm_baudrate_apply(g_odrive_config.comm.can_baudrate))
  {
    (void)can_comm_baudrate_apply(CAN_BAUDRATE_DEFAULT);
  }

  filter.filter_activate_enable = TRUE;
  filter.filter_mode = CAN_FILTER_MODE_ID_MASK;
  filter.filter_fifo = CAN_FILTER_FIFO0;
  filter.filter_number = 0u;
  filter.filter_bit = CAN_FILTER_32BIT;
  filter.filter_id_high = 0u;
  filter.filter_id_low = 0u;
  filter.filter_mask_high = 0u;
  filter.filter_mask_low = 0u;
  can_filter_init(CAN1, &filter);

  can_interrupt_enable(CAN1, CAN_RF0MIEN_INT, TRUE);
  nvic_irq_enable(CAN1_RX0_IRQn, IRQ_PRIORITY_COMMUNICATION, 0);

  s_rx_head = 0u;
  s_rx_tail = 0u;
  s_last_heartbeat_ms = system_millis();
  s_last_encoder_ms = system_millis();
  s_last_bus_vi_ms = system_millis();
}

static void can_handle_command(can_rx_item_t *item)
{
  uint32_t cmd = item->id & 0x1Fu;

  switch (cmd)
  {
    case CAN_SIMPLE_MSG_CO_NMT_CTRL:
    case CAN_SIMPLE_MSG_ODRIVE_HEARTBEAT:
    case CAN_SIMPLE_MSG_CO_HEARTBEAT_CMD:
      break;

    case CAN_SIMPLE_MSG_ODRIVE_ESTOP:
      axis_set_error(&g_axis, AXIS_ERROR_ESTOP_REQUESTED);
      break;

    case CAN_SIMPLE_MSG_GET_MOTOR_ERROR:
      can_send_u32(CAN_SIMPLE_MSG_GET_MOTOR_ERROR, g_axis.error & AXIS_ERROR_MASK_MOTOR);
      break;

    case CAN_SIMPLE_MSG_GET_ENCODER_ERROR:
      can_send_u32(CAN_SIMPLE_MSG_GET_ENCODER_ERROR, g_axis.encoder.error_count);
      break;

    case CAN_SIMPLE_MSG_GET_SENSORLESS_ERROR:
      can_send_u32(CAN_SIMPLE_MSG_GET_SENSORLESS_ERROR, 0u);
      break;

    case CAN_SIMPLE_MSG_SET_AXIS_NODE_ID:
      /* Range checked by the parameter table, as on the ASCII path. */
      (void)param_write_value("axis0.can.node_id", (float)can_read_i32_le(item->data));
      break;

    case CAN_SIMPLE_MSG_SET_AXIS_REQUESTED_STATE:
      axis_set_requested_state(&g_axis, (axis_state_t)can_read_i32_le(item->data));
      break;

    case CAN_SIMPLE_MSG_SET_AXIS_STARTUP_CONFIG:
      break;

    case CAN_SIMPLE_MSG_GET_ENCODER_ESTIMATES:
      can_comm_send_encoder_estimates();
      break;

    case CAN_SIMPLE_MSG_GET_ENCODER_COUNT:
    {
      uint8_t data[8];
      int32_t shadow = g_axis.encoder.shadow_count;
      int32_t raw = (int32_t)g_axis.encoder.raw;
      memcpy(data, &shadow, sizeof(int32_t));
      memcpy(data + 4, &raw, sizeof(int32_t));
      can_send(can_simple_id(CAN_SIMPLE_MSG_GET_ENCODER_COUNT), data, 8u);
      break;
    }

    case CAN_SIMPLE_MSG_SET_CONTROLLER_MODES:
      (void)param_write_value("axis0.controller.control_mode",
                              (float)can_read_i32_le(item->data));
      (void)param_write_value("axis0.controller.input_mode",
                              (float)can_read_i32_le(item->data + 4));
      break;

    case CAN_SIMPLE_MSG_SET_INPUT_POS:
    {
      float pos = can_read_float_le(item->data);
      int16_t vel_i16;
      int16_t torque_i16;
      memcpy(&vel_i16, item->data + 4, sizeof(int16_t));
      memcpy(&torque_i16, item->data + 6, sizeof(int16_t));
      axis_set_input_pos(&g_axis, pos);
      axis_set_input_vel(&g_axis, (float)vel_i16 * 0.001f);
      axis_set_input_torque(&g_axis, (float)torque_i16 * 0.001f);
      break;
    }

    case CAN_SIMPLE_MSG_SET_INPUT_VEL:
      axis_set_input_vel(&g_axis, can_read_float_le(item->data));
      axis_set_input_torque(&g_axis, can_read_float_le(item->data + 4));
      break;

    case CAN_SIMPLE_MSG_SET_INPUT_TORQUE:
      axis_set_input_torque(&g_axis, can_read_float_le(item->data));
      break;

    case CAN_SIMPLE_MSG_SET_LIMITS:
    {
      float vel_limit = can_read_float_le(item->data);
      float current_lim = can_read_float_le(item->data + 4);
      axis_set_limits(&g_axis, current_lim, vel_limit);
      break;
    }

    case CAN_SIMPLE_MSG_START_ANTICOGGING:
      break;

    case CAN_SIMPLE_MSG_SET_TRAJ_VEL_LIMIT:
      (void)param_write_value("axis0.controller.config.traj_vel_limit",
                              can_read_float_le(item->data));
      break;

    case CAN_SIMPLE_MSG_SET_TRAJ_ACCEL_LIMITS:
      (void)param_write_value("axis0.controller.config.traj_accel_limit",
                              can_read_float_le(item->data));
      (void)param_write_value("axis0.controller.config.traj_decel_limit",
                              can_read_float_le(item->data + 4));
      break;

    case CAN_SIMPLE_MSG_SET_TRAJ_INERTIA:
      g_axis.controller.inertia = can_read_float_le(item->data);
      break;

    case CAN_SIMPLE_MSG_GET_IQ:
      can_send_float_pair(CAN_SIMPLE_MSG_GET_IQ, g_axis.i_q_setpoint, g_axis.i_q_measured);
      break;

    case CAN_SIMPLE_MSG_GET_SENSORLESS_ESTIMATES:
      can_send_float_pair(CAN_SIMPLE_MSG_GET_SENSORLESS_ESTIMATES, 0.0f, 0.0f);
      break;

    case CAN_SIMPLE_MSG_RESET_ODRIVE:
      NVIC_SystemReset();
      break;

    case CAN_SIMPLE_MSG_GET_BUS_VOLTAGE_CURRENT:
      can_send_float_pair(CAN_SIMPLE_MSG_GET_BUS_VOLTAGE_CURRENT, g_axis.vbus_voltage, g_axis.i_bus_estimate);
      break;

    case CAN_SIMPLE_MSG_CLEAR_ERRORS:
      g_axis.error = AXIS_ERROR_NONE;
      axis_set_requested_state(&g_axis, AXIS_STATE_IDLE);
      break;

    case CAN_SIMPLE_MSG_SET_LINEAR_COUNT:
      encoder_set_linear_count(&g_axis.encoder, can_read_i32_le(item->data));
      break;

    case CAN_SIMPLE_MSG_SET_POS_GAIN:
      (void)param_write_value("axis0.controller.config.pos_gain",
                              can_read_float_le(item->data));
      break;

    case CAN_SIMPLE_MSG_SET_VEL_GAINS:
      (void)param_write_value("axis0.controller.config.vel_gain",
                              can_read_float_le(item->data));
      (void)param_write_value("axis0.controller.config.vel_integrator_gain",
                              can_read_float_le(item->data + 4));
      break;

    case CAN_SIMPLE_MSG_GET_ADC_VOLTAGE:
    {
      uint8_t gpio = item->data[0];
      float voltage = 0.0f;
      if (gpio == 0u)
      {
        voltage = g_axis.vbus_voltage;
      }
      else if (gpio == 1u)
      {
        voltage = g_axis.phase_current_a;
      }
      else if (gpio == 2u)
      {
        voltage = g_axis.phase_current_b;
      }
      else if (gpio == 3u)
      {
        voltage = g_axis.phase_current_c;
      }
      can_send_f32(CAN_SIMPLE_MSG_GET_ADC_VOLTAGE, voltage);
      break;
    }

    case CAN_SIMPLE_MSG_GET_CONTROLLER_ERROR:
      can_send_u32(CAN_SIMPLE_MSG_GET_CONTROLLER_ERROR,
                   g_axis.error & AXIS_ERROR_MASK_CONTROLLER);
      break;

    default:
      break;
  }
}

void can_comm_poll(void)
{
  can_rx_item_t item;
  uint32_t now_ms = system_millis();
  uint32_t heartbeat_rate = g_odrive_config.comm.can_heartbeat_rate_ms;

  while (can_rx_pop(&item))
  {
    if ((item.id >> 5) != g_axis.can_node_id)
    {
      continue;
    }

    axis_note_communication(&g_axis);
    can_handle_command(&item);
  }

  if ((heartbeat_rate > 0u) && ((now_ms - s_last_heartbeat_ms) >= heartbeat_rate))
  {
    s_last_heartbeat_ms = now_ms;
    can_comm_send_heartbeat();
  }

  if ((now_ms - s_last_encoder_ms) >= CAN_ENCODER_RATE_MS)
  {
    s_last_encoder_ms = now_ms;
    can_comm_send_encoder_estimates();
  }

  if ((now_ms - s_last_bus_vi_ms) >= CAN_BUS_VI_RATE_MS)
  {
    s_last_bus_vi_ms = now_ms;
    can_send_float_pair(CAN_SIMPLE_MSG_GET_BUS_VOLTAGE_CURRENT, g_axis.vbus_voltage, g_axis.i_bus_estimate);
  }
}

void can_comm_send_heartbeat(void)
{
  uint8_t data[8] = {0};

  /* Byte layout is fixed by the ODrive CAN Simple DBC:
   *   0..3 Axis_Error | 4 Axis_State | 5.0 Motor_Error_Flag
   *   6.0 Encoder_Error_Flag | 7.0 Controller_Error_Flag | 7.7 Trajectory_Done_Flag
   */
  data[0] = (uint8_t)(g_axis.error & 0xFFu);
  data[1] = (uint8_t)((g_axis.error >> 8) & 0xFFu);
  data[2] = (uint8_t)((g_axis.error >> 16) & 0xFFu);
  data[3] = (uint8_t)((g_axis.error >> 24) & 0xFFu);
  data[4] = (uint8_t)g_axis.current_state;
  data[5] = ((g_axis.error & AXIS_ERROR_MASK_MOTOR) != 0u) ? 1u : 0u;
  data[6] = (((g_axis.error & AXIS_ERROR_MASK_ENCODER) != 0u) ||
             (g_axis.encoder.error_count > 0u)) ? 1u : 0u;
  data[7] = ((g_axis.error & AXIS_ERROR_MASK_CONTROLLER) != 0u) ? 1u : 0u;
  if (g_axis.controller.trajectory_done)
  {
    data[7] |= 0x80u;
  }

  can_send(can_simple_id(CAN_SIMPLE_MSG_ODRIVE_HEARTBEAT), data, 8u);
}

void can_comm_send_encoder_estimates(void)
{
  can_send_float_pair(CAN_SIMPLE_MSG_GET_ENCODER_ESTIMATES, g_axis.pos_estimate, g_axis.vel_estimate);
}

void CAN1_RX0_IRQHandler(void)
{
  can_rx_message_type rx = {0};

  if (can_interrupt_flag_get(CAN1, CAN_RF0MN_FLAG) != RESET)
  {
    can_message_receive(CAN1, CAN_RX_FIFO0, &rx);
    can_rx_push(&rx);
  }
}
