#ifndef AT32_ODRIVE_FIBRE_ENDPOINTS_H
#define AT32_ODRIVE_FIBRE_ENDPOINTS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Fibre 0.1 endpoint tree for interoperability with the open-source ODrive GUI
 * / odrivetool.
 *
 * Endpoint 0 is the JSON descriptor.  The descriptor, the endpoint ids and the
 * per-endpoint kind/width tables behind the accessors below are GENERATED from
 * the s_params[] table in Src/param.c into Inc/fibre_endpoints_generated.h
 * (rebuild with tools/gen_fibre_endpoints.py after editing that table).  Every
 * other endpoint is a typed property, a read-only board identity member, or a
 * zero-argument function.  The JSON is a compact, whitespace-free UTF-8 list;
 * the server computes json_crc over these exact bytes.
 */

const char *fibre_endpoints_json(void);
uint32_t fibre_endpoints_json_length(void);

/*
 * Read/write an endpoint value in little-endian wire format.
 *
 * fibre_endpoint_read() returns the number of bytes written and never writes
 * more than buffer_len bytes.  Unknown or read-only endpoints return 0.
 *
 * fibre_endpoint_write() returns buffer_len for a property that accepted the
 * whole payload, 0 for a successfully called zero-argument function (which
 * consumes no payload), and 0 for an unknown, read-only or rejected write.  A
 * 0 therefore does not distinguish "already done" from "refused"; the caller
 * (fibre_server.c) discards the value either way.
 */
uint16_t fibre_endpoint_read(uint16_t endpoint_id, uint8_t *buffer, uint16_t buffer_len);
uint16_t fibre_endpoint_write(uint16_t endpoint_id, const uint8_t *buffer, uint16_t buffer_len);

#endif /* AT32_ODRIVE_FIBRE_ENDPOINTS_H */
