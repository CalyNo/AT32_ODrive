/*
 * Host-side tests for the Fibre 0.1 packet server (fibre_server.c).
 *
 * The full target has a generated endpoint table; here a tiny mock endpoint
 * tree exercises the packet framing, endpoint-0 JSON chunking, typed reads,
 * writes and trailer validation without any hardware dependency.
 */
#include "crc.h"
#include "fibre_endpoints.h"
#include "fibre_server.h"
#include "usb_fibre.h"

#include <stdio.h>
#include <string.h>

static const char s_mock_json[] =
    "[{\"name\":\"test_u32\",\"id\":1,\"type\":\"uint32\",\"access\":\"rw\"}]";

static uint32_t s_mock_value = 0x11223344u;
static uint8_t s_tx_packet[128];
static uint16_t s_tx_length;
static int g_failures;

const char *fibre_endpoints_json(void)
{
  return s_mock_json;
}

uint32_t fibre_endpoints_json_length(void)
{
  return (uint32_t)(sizeof(s_mock_json) - 1u);
}

uint16_t fibre_endpoint_read(uint16_t endpoint_id, uint8_t *buffer, uint16_t buffer_len)
{
  if ((endpoint_id == 1u) && (buffer_len >= 4u))
  {
    memcpy(buffer, &s_mock_value, sizeof(s_mock_value));
    return 4u;
  }
  return 0u;
}

uint16_t fibre_endpoint_write(uint16_t endpoint_id, const uint8_t *buffer, uint16_t buffer_len)
{
  if ((endpoint_id == 1u) && (buffer_len >= 4u))
  {
    memcpy(&s_mock_value, buffer, sizeof(s_mock_value));
    return 4u;
  }
  return 0u;
}

bool usb_fibre_send(const uint8_t *data, uint16_t length)
{
  if ((data == NULL) || (length > sizeof(s_tx_packet)))
  {
    return false;
  }
  memcpy(s_tx_packet, data, length);
  s_tx_length = length;
  return true;
}

static uint16_t read_le16(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void write_le16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value & 0xFFu);
  data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_le32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value & 0xFFu);
  data[1] = (uint8_t)((value >> 8) & 0xFFu);
  data[2] = (uint8_t)((value >> 16) & 0xFFu);
  data[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t build_request(uint8_t *packet,
                              uint16_t seq_no,
                              uint16_t endpoint_field,
                              uint16_t output_length,
                              const uint8_t *payload,
                              uint16_t payload_length,
                              uint16_t trailer)
{
  uint16_t length = 0u;

  write_le16(&packet[0], seq_no);
  write_le16(&packet[2], endpoint_field);
  write_le16(&packet[4], output_length);
  length = 6u;
  if ((payload != NULL) && (payload_length > 0u))
  {
    memcpy(&packet[length], payload, payload_length);
    length = (uint16_t)(length + payload_length);
  }
  write_le16(&packet[length], trailer);
  return (uint16_t)(length + 2u);
}

static void check(int condition, const char *name)
{
  if (!condition)
  {
    printf("FAIL %s\n", name);
    g_failures++;
  }
}

int main(void)
{
  uint8_t request[32];
  uint8_t offset[4];
  uint8_t value_le[4];
  uint16_t json_length = (uint16_t)fibre_endpoints_json_length();
  uint16_t json_crc;
  uint16_t length;

  (void)json_length;
  (void)length;

  g_failures = 0;
  fibre_server_init();
  json_crc = crc16_update(1u,
                          (const uint8_t *)fibre_endpoints_json(),
                          fibre_endpoints_json_length());

  /* Endpoint 0: first JSON chunk. */
  s_tx_length = 0u;
  write_le32(offset, 0u);
  length = build_request(request, 0x1234u, 0x8000u, 64u, offset, 4u, 1u);
  fibre_server_process_packet(request, length);
  {
    uint16_t expected = (uint16_t)(json_length < 61u ? json_length : 61u);
    check(s_tx_length == (uint16_t)(2u + expected), "ep0.chunk_length");
    check(read_le16(&s_tx_packet[0]) == 0x9234u, "ep0.seq");
    check(memcmp(&s_tx_packet[2], s_mock_json, expected) == 0, "ep0.data");
  }

  /* Endpoint 0: offset exactly at the end returns a zero-length response body. */
  s_tx_length = 0u;
  write_le32(offset, json_length);
  length = build_request(request, 0x1235u, 0x8000u, 64u, offset, 4u, 1u);
  fibre_server_process_packet(request, length);
  check(s_tx_length == 2u, "ep0.eof_length");

  /* Typed read: endpoint 1, 4 bytes, correct JSON CRC trailer. */
  s_tx_length = 0u;
  length = build_request(request, 0x2345u, 0x8001u, 4u, NULL, 0u, json_crc);
  fibre_server_process_packet(request, length);
  check(s_tx_length == 6u, "property.read_length");
  check(read_le16(&s_tx_packet[0]) == 0xA345u, "property.read_seq");
  check(s_tx_packet[2] == 0x44u && s_tx_packet[3] == 0x33u &&
        s_tx_packet[4] == 0x22u && s_tx_packet[5] == 0x11u,
        "property.read_value");

  /* Typed write: payload changes the mock value; ACK is seq-only. */
  s_tx_length = 0u;
  write_le32(value_le, 0x55667788u);
  length = build_request(request, 0x2346u, 0x8001u, 0u, value_le, 4u, json_crc);
  fibre_server_process_packet(request, length);
  check(s_tx_length == 2u, "property.write_ack_length");
  check(s_mock_value == 0x55667788u, "property.write_value");

  /* Wrong trailer must be ignored. */
  s_tx_length = 0u;
  length = build_request(request, 0x3456u, 0x8001u, 4u, NULL, 0u, 0x0000u);
  fibre_server_process_packet(request, length);
  check(s_tx_length == 0u, "trailer.reject");

  /* Unknown endpoint returns a seq-only ACK rather than a body. */
  s_tx_length = 0u;
  length = build_request(request, 0x4567u, 0x8002u, 4u, NULL, 0u, json_crc);
  fibre_server_process_packet(request, length);
  check(s_tx_length == 2u, "unknown.ack");

  /* Short packet must be ignored. */
  s_tx_length = 0u;
  fibre_server_process_packet(request, 4u);
  check(s_tx_length == 0u, "short.reject");

  if (g_failures == 0)
  {
    printf("PASS test_fibre_packet\n");
    return 0;
  }

  printf("FAIL test_fibre_packet: %d checks failed\n", g_failures);
  return 1;
}
