#include "uart_comm.h"
#include "irq_priority.h"
#include "axis.h"
#include "board.h"
#include "config.h"
#include "param.h"
#include "usb_cdc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UART_RX_BUFFER_SIZE 256u
#define UART_LINE_BUFFER_SIZE 128u

static volatile uint8_t s_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static char s_line[UART_LINE_BUFFER_SIZE];
static uint16_t s_line_len;

static void uart_rx_push(uint8_t byte)
{
  uint16_t next = (uint16_t)((s_rx_head + 1u) % UART_RX_BUFFER_SIZE);

  if (next != s_rx_tail)
  {
    s_rx_buffer[s_rx_head] = byte;
    s_rx_head = next;
  }
}

void uart_comm_feed(uint8_t byte)
{
  uart_rx_push(byte);
}

static bool uart_rx_pop(uint8_t *byte)
{
  if (s_rx_tail == s_rx_head)
  {
    return false;
  }

  *byte = s_rx_buffer[s_rx_tail];
  s_rx_tail = (uint16_t)((s_rx_tail + 1u) % UART_RX_BUFFER_SIZE);
  return true;
}

void uart_comm_init(void)
{
  crm_periph_clock_enable(CRM_USART3_PERIPH_CLOCK, TRUE);

  usart_init(USART3, UART_BAUDRATE_DEFAULT, USART_DATA_8BITS, USART_STOP_1_BIT);

  /*
   * The transmitter and receiver must be switched on before the peripheral is
   * enabled (same order as the SDK examples).  Without TEN the TDBE flag never
   * becomes set, so board_uart_write() polls forever and the board looks
   * completely dead (no boot trace, no banner, no parameter replies); without
   * REN the host cannot send anything and the RDBF interrupt never fires.
   */
  usart_transmitter_enable(USART3, TRUE);
  usart_receiver_enable(USART3, TRUE);

  usart_interrupt_enable(USART3, USART_RDBF_INT, TRUE);
  nvic_irq_enable(USART3_IRQn, IRQ_PRIORITY_COMMUNICATION, 0);
  usart_enable(USART3, TRUE);

  s_rx_head = 0u;
  s_rx_tail = 0u;
  s_line_len = 0u;

  /* Second host transport; a no-op when the clock is degraded. */
  usb_cdc_init();
}

void uart_comm_write(const uint8_t *data, uint32_t len)
{
  board_uart_write(data, len);
  /* Broadcast: a host on either port sees the complete conversation. */
  usb_cdc_write(data, len);
}

void uart_comm_printf(const char *fmt, ...)
{
  char buffer[160];
  va_list args;
  int len;

  va_start(args, fmt);
  len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (len > 0)
  {
    uint32_t n = (uint32_t)len;
    if (n > sizeof(buffer) - 1u)
    {
      n = sizeof(buffer) - 1u;
    }
    uart_comm_write((const uint8_t *)buffer, n);
  }
}

/*
 * ODrive ASCII command layer.
 *
 * Response semantics follow ODrive:
 *   - a successful read answers with the bare value (no path prefix);
 *   - a successful write answers nothing at all;
 *   - failures answer with ODrive's wording.
 * Host libraries depend on this: the Arduino ODrive library, for example, does
 * readString().toFloat() on the reply of "r axis0.encoder.pos_estimate".
 */
static const char *uart_args_after_command(const char *line)
{
  const char *p = line + 1;

  while ((*p == ' ') || (*p == '\t'))
  {
    p++;
  }
  return p;
}

/* Consumes a leading motor index.  Returns the remaining argument string, or
 * NULL when the command was already rejected (single axis: only motor 0). */
static const char *uart_take_motor(const char *args)
{
  const char *p = args;
  unsigned motor = 0u;
  bool have_digit = false;

  while ((*p == ' ') || (*p == '\t'))
  {
    p++;
  }

  while ((*p >= '0') && (*p <= '9'))
  {
    motor = (motor * 10u) + (unsigned)(*p - '0');
    p++;
    have_digit = true;
  }

  if (!have_digit)
  {
    uart_comm_printf("invalid command format\r\n");
    return NULL;
  }

  if (motor != 0u)
  {
    uart_comm_printf("invalid motor %u\r\n", motor);
    return NULL;
  }

  while ((*p == ' ') || (*p == '\t'))
  {
    p++;
  }
  return p;
}

/* Parses up to max_values whitespace separated floats, the same way the 'w'
 * path parses its value.  Returns how many values were found. */
static uint32_t uart_parse_floats(const char *text, float *values, uint32_t max_values)
{
  const char *cursor = text;
  uint32_t count = 0u;

  while (count < max_values)
  {
    char *end;
    float value = strtof(cursor, &end);

    if (end == cursor)
    {
      break;
    }
    values[count] = value;
    count++;
    cursor = end;
  }

  return count;
}

static void uart_write_property(char *path_and_value)
{
  char *value_text;
  param_result_t result;

  value_text = strchr(path_and_value, ' ');
  if (value_text == NULL)
  {
    value_text = strchr(path_and_value, '\t');
  }
  if (value_text != NULL)
  {
    *value_text = '\0';
    value_text++;
    while ((*value_text == ' ') || (*value_text == '\t'))
    {
      value_text++;
    }
  }

  if (param_is_save_path(path_and_value))
  {
    if (!param_save_configuration())
    {
      uart_comm_printf("save failed\r\n");
    }
    return;
  }

  result = param_write(path_and_value, value_text);

  switch (result)
  {
    case PARAM_RESULT_OK:
      /* ODrive stays silent on a successful write. */
      break;

    case PARAM_RESULT_UNKNOWN:
      uart_comm_printf("invalid property\r\n");
      break;

    case PARAM_RESULT_MISSING_VALUE:
      uart_comm_printf("invalid command format\r\n");
      break;

    case PARAM_RESULT_READ_ONLY:
      uart_comm_printf("not implemented\r\n");
      break;

    case PARAM_RESULT_OUT_OF_RANGE:
      uart_comm_printf("invalid value\r\n");
      break;

    default:
      break;
  }
}

/* 'p <motor> <pos> [vel_ff] [torque_ff]' -- ODrive leaves input_mode alone. */
static void uart_set_position(const char *args)
{
  const char *rest = uart_take_motor(args);
  float values[3];
  uint32_t count;

  if (rest == NULL)
  {
    return;
  }

  count = uart_parse_floats(rest, values, 3u);
  if (count < 1u)
  {
    uart_comm_printf("invalid command format\r\n");
    return;
  }

  (void)param_write_value("axis0.controller.control_mode", (float)CONTROL_MODE_POSITION_CONTROL);
  (void)param_write_value("axis0.controller.input_pos", values[0]);
  if (count >= 2u)
  {
    (void)param_write_value("axis0.controller.input_vel", values[1]);
  }
  if (count >= 3u)
  {
    (void)param_write_value("axis0.controller.input_torque", values[2]);
  }
}

/* 'v <motor> <vel> [torque_ff]' */
static void uart_set_velocity(const char *args)
{
  const char *rest = uart_take_motor(args);
  float values[2];
  uint32_t count;

  if (rest == NULL)
  {
    return;
  }

  count = uart_parse_floats(rest, values, 2u);
  if (count < 1u)
  {
    uart_comm_printf("invalid command format\r\n");
    return;
  }

  (void)param_write_value("axis0.controller.control_mode", (float)CONTROL_MODE_VELOCITY_CONTROL);
  (void)param_write_value("axis0.controller.input_vel", values[0]);
  if (count >= 2u)
  {
    (void)param_write_value("axis0.controller.input_torque", values[1]);
  }
}

/* 'c <motor> <current>' -- torque control, current in A. */
static void uart_set_torque(const char *args)
{
  const char *rest = uart_take_motor(args);
  float values[1];

  if (rest == NULL)
  {
    return;
  }

  if (uart_parse_floats(rest, values, 1u) < 1u)
  {
    uart_comm_printf("invalid command format\r\n");
    return;
  }

  (void)param_write_value("axis0.controller.control_mode", (float)CONTROL_MODE_TORQUE_CONTROL);
  (void)param_write_value("axis0.controller.input_torque", values[0]);
}

/* 't <motor> <destination>' -- trapezoidal move, enables TRAP_TRAJ input mode. */
static void uart_set_trapezoid(const char *args)
{
  const char *rest = uart_take_motor(args);
  float values[1];

  if (rest == NULL)
  {
    return;
  }

  if (uart_parse_floats(rest, values, 1u) < 1u)
  {
    uart_comm_printf("invalid command format\r\n");
    return;
  }

  (void)param_write_value("axis0.controller.input_mode", (float)INPUT_MODE_TRAP_TRAJ);
  (void)param_write_value("axis0.controller.control_mode", (float)CONTROL_MODE_POSITION_CONTROL);
  (void)param_write_value("axis0.controller.input_pos", values[0]);
}

/* 'f <motor>' -> "pos vel" */
static void uart_feedback(const char *args)
{
  if (uart_take_motor(args) == NULL)
  {
    return;
  }

  uart_comm_printf("%.6f %.6f\r\n", (double)g_axis.pos_estimate, (double)g_axis.vel_estimate);
}

/* 'u <motor>' -- feed the communication watchdog.  uart_handle_line() already
 * notes the traffic, so this is only a validity check. */
static void uart_update_watchdog(const char *args)
{
  (void)uart_take_motor(args);
}

/* 'ss' save, 'sr' reboot, 'sc' clear errors, 'se' erase config. */
static void uart_system_command(const char *line)
{
  switch (line[1])
  {
    case 's':
      if (!param_save_configuration())
      {
        uart_comm_printf("save failed\r\n");
      }
      break;

    case 'r':
      NVIC_SystemReset();
      break;

    case 'c':
      /* Same path as "w axis0.clear_errors 1" and the CAN Clear_Errors command. */
      (void)param_write_value("axis0.clear_errors", 1.0f);
      break;

    case 'e':
      /* Erasing would drop the calibration records; not offered over ASCII. */
      uart_comm_printf("not implemented\r\n");
      break;

    default:
      uart_comm_printf("unknown command\r\n");
      break;
  }
}

static void uart_handle_line(char *line)
{
  char command;

  while ((*line == ' ') || (*line == '\t') || (*line == '\r') || (*line == '\n'))
  {
    line++;
  }

  if (*line == '\0')
  {
    return;
  }

  axis_note_communication(&g_axis);

  command = *line;

  switch (command)
  {
    case 'r':
      param_print(uart_args_after_command(line));
      break;

    case 'w':
      uart_write_property((char *)uart_args_after_command(line));
      break;

    case 'p':
      uart_set_position(uart_args_after_command(line));
      break;

    case 'v':
      uart_set_velocity(uart_args_after_command(line));
      break;

    case 'c':
      uart_set_torque(uart_args_after_command(line));
      break;

    case 't':
      uart_set_trapezoid(uart_args_after_command(line));
      break;

    case 'f':
      uart_feedback(uart_args_after_command(line));
      break;

    case 'u':
      uart_update_watchdog(uart_args_after_command(line));
      break;

    case 's':
      uart_system_command(line);
      break;

    default:
      uart_comm_printf("unknown command\r\n");
      break;
  }
}

void uart_comm_poll(void)
{
  uint8_t byte;

  usb_cdc_poll();

  while (uart_rx_pop(&byte))
  {
    if ((byte == '\n') || (byte == '\r'))
    {
      if (s_line_len > 0u)
      {
        s_line[s_line_len] = '\0';
        uart_handle_line(s_line);
        s_line_len = 0u;
      }
    }
    else if (s_line_len < (UART_LINE_BUFFER_SIZE - 1u))
    {
      s_line[s_line_len++] = (char)byte;
    }
    else
    {
      s_line_len = 0u;
    }
  }
}

void USART3_IRQHandler(void)
{
  if (usart_interrupt_flag_get(USART3, USART_RDBF_FLAG) != RESET)
  {
    uint16_t data = usart_data_receive(USART3);
    uart_rx_push((uint8_t)data);
  }
}
