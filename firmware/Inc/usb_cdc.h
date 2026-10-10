#ifndef AT32_ODRIVE_USB_CDC_H
#define AT32_ODRIVE_USB_CDC_H

#include <stdbool.h>
#include <stdint.h>

/*
 * USB FS CDC-ACM "virtual COM port" (OTGFS1: PA11 = DM, PA12 = DP).
 *
 * The port speaks exactly the same ASCII protocol as USART3 (PB10/PB11): both
 * transports feed the same parser in uart_comm.c and both receive every reply,
 * so the parameter table, `r`/`w`, `ss`/`sr`/`sc` and the boot trace work
 * identically over either link.
 *
 * usb_cdc_poll() is called from the 1 ms communication task and moves at most
 * one bulk packet per direction per call, which is far more than a debug
 * console needs.
 *
 * Note: USB needs an exact 48 MHz, which this board takes from the 288 MHz PLL
 * divided by 6.  When the 8 MHz oscillator does not start (degraded clock), USB
 * is not initialised at all; usb_cdc_connected() stays false.
 */
void usb_cdc_init(void);
void usb_cdc_poll(void);

/* Queue bytes for the host (non-blocking, drops on ring overflow). */
void usb_cdc_write(const uint8_t *data, uint32_t len);

/* True once the host has enumerated and configured the CDC interface. */
bool usb_cdc_connected(void);

/* Diagnostics, exposed as the usb.* parameters (updated by usb_cdc_poll(),
 * which runs in the 1 ms communication task, hence plain scalars). */
extern uint32_t g_usb_cdc_connected;
extern uint32_t g_usb_cdc_rx_bytes;
extern uint32_t g_usb_cdc_tx_dropped;

#endif /* AT32_ODRIVE_USB_CDC_H */
