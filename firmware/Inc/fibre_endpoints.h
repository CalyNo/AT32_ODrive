#ifndef AT32_ODRIVE_FIBRE_ENDPOINTS_H
#define AT32_ODRIVE_FIBRE_ENDPOINTS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Minimal Fibre 0.1 endpoint table for interoperability with the open-source
 * ODrive GUI / odrivetool.
 *
 * Endpoint 0 is the JSON descriptor.  Every other endpoint is a typed property
 * or object member.  The JSON is a compact, whitespace-free UTF-8 list; the
 * server computes json_crc over these exact bytes.
 */

const char *fibre_endpoints_json(void);
uint32_t fibre_endpoints_json_length(void);

/*
 * Read/write an endpoint value in little-endian wire format.
 *
 * fibre_endpoint_read() returns the number of bytes written and never writes
 * more than buffer_len bytes.  Unknown or read-only endpoints return 0.
 * fibre_endpoint_write() returns the number of bytes consumed; unknown or
 * read-only endpoints return 0.
 */
uint16_t fibre_endpoint_read(uint16_t endpoint_id, uint8_t *buffer, uint16_t buffer_len);
uint16_t fibre_endpoint_write(uint16_t endpoint_id, const uint8_t *buffer, uint16_t buffer_len);

#endif /* AT32_ODRIVE_FIBRE_ENDPOINTS_H */
