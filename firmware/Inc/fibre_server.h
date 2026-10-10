#ifndef AT32_ODRIVE_FIBRE_SERVER_H
#define AT32_ODRIVE_FIBRE_SERVER_H

#include <stdint.h>

/*
 * Minimal Fibre 0.1 packet-based server (the protocol used by the
 * open-source ODrive GUI / odrivetool over the USB vendor interface).
 *
 * Request packet:
 *   [seq u16 LE][endpoint u16 LE][output_len u16 LE][payload...][trailer u16 LE]
 *
 * Response packet:
 *   [seq|0x8000 u16 LE][data...]
 *
 * Endpoint 0 returns the endpoint JSON descriptor in chunks.  All other
 * endpoints are typed little-endian properties handled by fibre_endpoints.c.
 */

void fibre_server_init(void);

/* Process one complete USB bulk packet and transmit any response. */
void fibre_server_process_packet(const uint8_t *packet, uint16_t length);

#endif /* AT32_ODRIVE_FIBRE_SERVER_H */
