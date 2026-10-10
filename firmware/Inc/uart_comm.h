#ifndef AT32_ODRIVE_UART_COMM_H
#define AT32_ODRIVE_UART_COMM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Host link: the ODrive-style ASCII protocol over USART3 (PB10/PB11) and, when
 * the USB CDC interface is enumerated, over USB as well.  Both transports feed
 * the same parser and both receive every reply (uart_comm_write() broadcasts),
 * so the parameter table behaves identically on either port.
 */
void uart_comm_init(void);
void uart_comm_poll(void);
void uart_comm_write(const uint8_t *data, uint32_t len);
void uart_comm_printf(const char *fmt, ...);

/* Feed one received byte into the line assembler (USART3 ISR, USB poll). */
void uart_comm_feed(uint8_t byte);

#endif /* AT32_ODRIVE_UART_COMM_H */
