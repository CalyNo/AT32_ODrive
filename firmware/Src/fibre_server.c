#include "fibre_server.h"

#include "crc.h"
#include "fibre_endpoints.h"
#include "usb_fibre.h"

#include <string.h>

#define FIBRE_PROTOCOL_VERSION       (1u)
#define FIBRE_MAX_RESPONSE_DATA      (61u) /* seq(2) + data(61) = 63 <= USB FS bulk MTU */
#define FIBRE_MIN_REQUEST_LENGTH     (8u)

static uint16_t s_json_crc;
static uint8_t s_tx_packet[2u + FIBRE_MAX_RESPONSE_DATA];

static uint16_t read_le16(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_le32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

static void write_le16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value & 0xFFu);
  data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

void fibre_server_init(void)
{
  s_json_crc = crc16_update(1u,
                            (const uint8_t *)fibre_endpoints_json(),
                            fibre_endpoints_json_length());
}

void fibre_server_process_packet(const uint8_t *packet, uint16_t length)
{
  uint16_t seq_no;
  uint16_t endpoint_field;
  uint16_t endpoint_id;
  uint16_t output_length;
  uint16_t trailer;
  uint16_t expected_trailer;
  const uint8_t *payload;
  uint16_t payload_length;
  uint16_t response_length = 2u;

  if ((packet == NULL) || (length < FIBRE_MIN_REQUEST_LENGTH))
  {
    return;
  }

  seq_no = read_le16(&packet[0]);
  endpoint_field = read_le16(&packet[2]);
  output_length = read_le16(&packet[4]);
  trailer = read_le16(&packet[length - 2u]);

  endpoint_id = (uint16_t)(endpoint_field & 0x7FFFu);
  payload = &packet[6];
  payload_length = (uint16_t)(length - FIBRE_MIN_REQUEST_LENGTH);

  /*
   * The trailer confirms that both sides use the same object model.  Endpoint
   * 0 is the JSON descriptor itself and therefore uses the protocol version;
   * all other endpoints use the descriptor CRC.
   */
  expected_trailer = (endpoint_id == 0u) ? FIBRE_PROTOCOL_VERSION : s_json_crc;
  if (trailer != expected_trailer)
  {
    return;
  }

  if (endpoint_id == 0u)
  {
    uint32_t offset;
    const char *json = fibre_endpoints_json();
    uint32_t json_length = fibre_endpoints_json_length();

    if (payload_length < 4u)
    {
      return;
    }

    offset = read_le32(payload);

    /*
     * Spec-defined sentinel: asking for offset 0xFFFFFFFF returns the
     * descriptor's 32-bit version id instead of a slice of the descriptor.
     * This mirrors the official endpoint0_handler(), and the id is the same
     * value the host derives from the descriptor it read:
     *   (json_crc << 16) | crc16_update(json_crc, json, json_length)
     * Emitted little-endian, and only when the client asked for at least those
     * four bytes -- otherwise the response stays empty like any other
     * unsatisfiable request.
     */
    if (offset == 0xFFFFFFFFu)
    {
      uint32_t json_crc = (uint32_t)s_json_crc;
      uint32_t version_id = (json_crc << 16) |
                            (uint32_t)crc16_update((uint16_t)json_crc,
                                                   (const uint8_t *)json,
                                                   json_length);
      uint16_t written = 0u;

      if (output_length >= 4u)
      {
        s_tx_packet[2] = (uint8_t)(version_id & 0xFFu);
        s_tx_packet[3] = (uint8_t)((version_id >> 8) & 0xFFu);
        s_tx_packet[4] = (uint8_t)((version_id >> 16) & 0xFFu);
        s_tx_packet[5] = (uint8_t)((version_id >> 24) & 0xFFu);
        written = 4u;
      }

      response_length = (uint16_t)(2u + written);
    }
    else if ((offset < json_length) && (output_length > 0u))
    {
      uint32_t available = json_length - offset;
      uint32_t copy_length = available;

      if (copy_length > output_length)
      {
        copy_length = output_length;
      }
      if (copy_length > FIBRE_MAX_RESPONSE_DATA)
      {
        copy_length = FIBRE_MAX_RESPONSE_DATA;
      }

      memcpy(&s_tx_packet[2], &json[offset], copy_length);
      response_length = (uint16_t)(2u + copy_length);
    }
  }
  else
  {
    /*
     * Always offer the payload to the endpoint, even when it is empty: a
     * zero-argument function endpoint is called with a zero-length payload, so
     * gating this on payload_length > 0 turned save_configuration() into a
     * silent no-op -- the GUI's "save configuration" button did nothing and
     * reported no error.  The endpoint dispatcher decides what the length
     * means: an exact codec-width payload is a property write, a zero-length
     * payload is a function call, anything else is neither.  An empty payload
     * is therefore never interpreted as "write zero".
     */
    (void)fibre_endpoint_write(endpoint_id, payload, payload_length);

    if (output_length > 0u)
    {
      uint16_t max_length = output_length;

      if (max_length > FIBRE_MAX_RESPONSE_DATA)
      {
        max_length = FIBRE_MAX_RESPONSE_DATA;
      }

      response_length = (uint16_t)(2u + fibre_endpoint_read(endpoint_id,
                                                            &s_tx_packet[2],
                                                            max_length));
    }
  }

  if ((endpoint_field & 0x8000u) != 0u)
  {
    write_le16(&s_tx_packet[0], (uint16_t)(seq_no | 0x8000u));
    (void)usb_fibre_send(s_tx_packet, response_length);
  }
}
