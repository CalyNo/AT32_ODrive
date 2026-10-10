#ifndef AT32_ODRIVE_USB_FIBRE_H
#define AT32_ODRIVE_USB_FIBRE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * USB transport for the Fibre 0.1 endpoint used by the open-source ODrive GUI.
 *
 * The USB descriptor changes live in the USB device class files; this module
 * only moves complete USB bulk packets between the vendor endpoint and the
 * Fibre packet server.  Simulation/degraded-clock builds never enumerate USB,
 * so usb_fibre_poll() becomes a no-op.
 */

void usb_fibre_init(void);
void usb_fibre_poll(void);

/* Queue one complete Fibre response packet on the vendor IN endpoint. */
bool usb_fibre_send(const uint8_t *data, uint16_t length);

#endif /* AT32_ODRIVE_USB_FIBRE_H */
