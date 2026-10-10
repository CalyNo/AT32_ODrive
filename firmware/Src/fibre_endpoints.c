#include "fibre_endpoints.h"

#include "axis.h"
#include "config.h"

#include <string.h>

/*
 * Minimal endpoint tree for the open-source ODrive GUI / legacy Fibre 0.1
 * clients.  See Inc/fibre_endpoints.h.  The JSON below is intentionally
 * whitespace-free: json_crc is computed over the exact bytes and both sides
 * must hash the same descriptor.
 */

typedef enum
{
  FIBRE_EP_FW_VERSION_MAJOR = 1,
  FIBRE_EP_FW_VERSION_MINOR = 2,
  FIBRE_EP_FW_VERSION_REVISION = 3,
  FIBRE_EP_HW_VERSION_MAJOR = 4,
  FIBRE_EP_HW_VERSION_MINOR = 5,
  FIBRE_EP_HW_VERSION_VARIANT = 6,
  FIBRE_EP_SERIAL_NUMBER = 7,
  FIBRE_EP_VBUS_VOLTAGE = 8,
  FIBRE_EP_AXIS0_ERROR = 9,
  FIBRE_EP_AXIS0_CURRENT_STATE = 10,
  FIBRE_EP_AXIS0_REQUESTED_STATE = 11,
  FIBRE_EP_AXIS0_ENCODER_POS_ESTIMATE = 12,
  FIBRE_EP_AXIS0_ENCODER_VEL_ESTIMATE = 13,
  FIBRE_EP_AXIS0_CONTROLLER_INPUT_POS = 14,
  FIBRE_EP_AXIS0_CONTROLLER_INPUT_VEL = 15,
  FIBRE_EP_AXIS0_CONTROLLER_INPUT_TORQUE = 16
} fibre_endpoint_id_t;

/*
 * The board has no official ODrive product-line identity.  Keep the three
 * hw_version fields at zero so hosts can display "unknown board" instead of
 * treating this as an ODrive Pro/S1/Micro.  This is a deliberate compatibility
 * choice, not a claim about physical hardware revision.
 */
#define FIBRE_HW_VERSION_MAJOR   (0u)
#define FIBRE_HW_VERSION_MINOR   (0u)
#define FIBRE_HW_VERSION_VARIANT (0u)

#define AT32_UID_WORD0 (*(volatile uint32_t *)0x1FFFF7E8u)
#define AT32_UID_WORD1 (*(volatile uint32_t *)0x1FFFF7ECu)

static const char s_endpoint_json[] =
    "["
      "{\"name\":\"fw_version_major\",\"id\":1,\"type\":\"uint8\",\"access\":\"r\"},"
      "{\"name\":\"fw_version_minor\",\"id\":2,\"type\":\"uint8\",\"access\":\"r\"},"
      "{\"name\":\"fw_version_revision\",\"id\":3,\"type\":\"uint8\",\"access\":\"r\"},"
      "{\"name\":\"hw_version_major\",\"id\":4,\"type\":\"uint8\",\"access\":\"r\"},"
      "{\"name\":\"hw_version_minor\",\"id\":5,\"type\":\"uint8\",\"access\":\"r\"},"
      "{\"name\":\"hw_version_variant\",\"id\":6,\"type\":\"uint8\",\"access\":\"r\"},"
      "{\"name\":\"serial_number\",\"id\":7,\"type\":\"uint64\",\"access\":\"r\"},"
      "{\"name\":\"vbus_voltage\",\"id\":8,\"type\":\"float\",\"access\":\"r\"},"
      "{\"name\":\"axis0\",\"type\":\"object\",\"members\":["
        "{\"name\":\"error\",\"id\":9,\"type\":\"uint32\",\"access\":\"r\"},"
        "{\"name\":\"current_state\",\"id\":10,\"type\":\"int32\",\"access\":\"r\"},"
        "{\"name\":\"requested_state\",\"id\":11,\"type\":\"uint32\",\"access\":\"rw\"},"
        "{\"name\":\"encoder\",\"type\":\"object\",\"members\":["
          "{\"name\":\"pos_estimate\",\"id\":12,\"type\":\"float\",\"access\":\"r\"},"
          "{\"name\":\"vel_estimate\",\"id\":13,\"type\":\"float\",\"access\":\"r\"}"
        "]},"
        "{\"name\":\"controller\",\"type\":\"object\",\"members\":["
          "{\"name\":\"input_pos\",\"id\":14,\"type\":\"float\",\"access\":\"rw\"},"
          "{\"name\":\"input_vel\",\"id\":15,\"type\":\"float\",\"access\":\"rw\"},"
          "{\"name\":\"input_torque\",\"id\":16,\"type\":\"float\",\"access\":\"rw\"}"
        "]}"
      "]}"
    "]";

static uint16_t put_u8(uint8_t *buffer, uint16_t buffer_len, uint8_t value)
{
  if (buffer_len < 1u)
  {
    return 0u;
  }

  buffer[0] = value;
  return 1u;
}

static uint16_t put_i32(uint8_t *buffer, uint16_t buffer_len, int32_t value)
{
  if (buffer_len < 4u)
  {
    return 0u;
  }

  memcpy(buffer, &value, sizeof(value));
  return 4u;
}

static uint16_t put_u32(uint8_t *buffer, uint16_t buffer_len, uint32_t value)
{
  if (buffer_len < 4u)
  {
    return 0u;
  }

  memcpy(buffer, &value, sizeof(value));
  return 4u;
}

static uint16_t put_u64(uint8_t *buffer, uint16_t buffer_len, uint64_t value)
{
  if (buffer_len < 8u)
  {
    return 0u;
  }

  memcpy(buffer, &value, sizeof(value));
  return 8u;
}

static uint16_t put_f32(uint8_t *buffer, uint16_t buffer_len, float value)
{
  if (buffer_len < 4u)
  {
    return 0u;
  }

  memcpy(buffer, &value, sizeof(value));
  return 4u;
}

static uint32_t get_u32(const uint8_t *buffer, uint16_t buffer_len)
{
  uint32_t value = 0u;

  if (buffer_len >= 4u)
  {
    memcpy(&value, buffer, sizeof(value));
  }

  return value;
}

static float get_f32(const uint8_t *buffer, uint16_t buffer_len)
{
  float value = 0.0f;

  if (buffer_len >= 4u)
  {
    memcpy(&value, buffer, sizeof(value));
  }

  return value;
}

const char *fibre_endpoints_json(void)
{
  return s_endpoint_json;
}

uint32_t fibre_endpoints_json_length(void)
{
  return (uint32_t)(sizeof(s_endpoint_json) - 1u);
}

uint16_t fibre_endpoint_read(uint16_t endpoint_id, uint8_t *buffer, uint16_t buffer_len)
{
  uint64_t serial_number;

  switch ((fibre_endpoint_id_t)endpoint_id)
  {
    case FIBRE_EP_FW_VERSION_MAJOR:
      return put_u8(buffer, buffer_len, (uint8_t)FIRMWARE_VERSION_MAJOR);
    case FIBRE_EP_FW_VERSION_MINOR:
      return put_u8(buffer, buffer_len, (uint8_t)FIRMWARE_VERSION_MINOR);
    case FIBRE_EP_FW_VERSION_REVISION:
      return put_u8(buffer, buffer_len, (uint8_t)FIRMWARE_VERSION_REVISION);
    case FIBRE_EP_HW_VERSION_MAJOR:
      return put_u8(buffer, buffer_len, (uint8_t)FIBRE_HW_VERSION_MAJOR);
    case FIBRE_EP_HW_VERSION_MINOR:
      return put_u8(buffer, buffer_len, (uint8_t)FIBRE_HW_VERSION_MINOR);
    case FIBRE_EP_HW_VERSION_VARIANT:
      return put_u8(buffer, buffer_len, (uint8_t)FIBRE_HW_VERSION_VARIANT);
    case FIBRE_EP_SERIAL_NUMBER:
      serial_number = ((uint64_t)AT32_UID_WORD0 << 32) | (uint64_t)AT32_UID_WORD1;
      return put_u64(buffer, buffer_len, serial_number);
    case FIBRE_EP_VBUS_VOLTAGE:
      return put_f32(buffer, buffer_len, g_axis.vbus_voltage);
    case FIBRE_EP_AXIS0_ERROR:
      return put_u32(buffer, buffer_len, g_axis.error);
    case FIBRE_EP_AXIS0_CURRENT_STATE:
      return put_i32(buffer, buffer_len, (int32_t)g_axis.current_state);
    case FIBRE_EP_AXIS0_REQUESTED_STATE:
      return put_u32(buffer, buffer_len, (uint32_t)g_axis.requested_state);
    case FIBRE_EP_AXIS0_ENCODER_POS_ESTIMATE:
      return put_f32(buffer, buffer_len, g_axis.pos_estimate);
    case FIBRE_EP_AXIS0_ENCODER_VEL_ESTIMATE:
      return put_f32(buffer, buffer_len, g_axis.vel_estimate);
    case FIBRE_EP_AXIS0_CONTROLLER_INPUT_POS:
      return put_f32(buffer, buffer_len, g_axis.controller.pos_input);
    case FIBRE_EP_AXIS0_CONTROLLER_INPUT_VEL:
      return put_f32(buffer, buffer_len, g_axis.controller.vel_input);
    case FIBRE_EP_AXIS0_CONTROLLER_INPUT_TORQUE:
      return put_f32(buffer, buffer_len, g_axis.controller.torque_input);
    default:
      return 0u;
  }
}

uint16_t fibre_endpoint_write(uint16_t endpoint_id, const uint8_t *buffer, uint16_t buffer_len)
{
  switch ((fibre_endpoint_id_t)endpoint_id)
  {
    case FIBRE_EP_AXIS0_REQUESTED_STATE:
      if (buffer_len < 4u)
      {
        return 0u;
      }
      axis_set_requested_state(&g_axis, (axis_state_t)get_u32(buffer, buffer_len));
      return 4u;

    case FIBRE_EP_AXIS0_CONTROLLER_INPUT_POS:
      if (buffer_len < 4u)
      {
        return 0u;
      }
      axis_set_input_pos(&g_axis, get_f32(buffer, buffer_len));
      return 4u;

    case FIBRE_EP_AXIS0_CONTROLLER_INPUT_VEL:
      if (buffer_len < 4u)
      {
        return 0u;
      }
      axis_set_input_vel(&g_axis, get_f32(buffer, buffer_len));
      return 4u;

    case FIBRE_EP_AXIS0_CONTROLLER_INPUT_TORQUE:
      if (buffer_len < 4u)
      {
        return 0u;
      }
      axis_set_input_torque(&g_axis, get_f32(buffer, buffer_len));
      return 4u;

    default:
      return 0u;
  }
}
