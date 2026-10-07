#ifndef AT32_ODRIVE_UART_COMM_H
#define AT32_ODRIVE_UART_COMM_H

#include <stdbool.h>
#include <stdint.h>

void uart_comm_init(void);
void uart_comm_poll(void);
void uart_comm_write(const uint8_t *data, uint32_t len);
void uart_comm_printf(const char *fmt, ...);

#endif /* AT32_ODRIVE_UART_COMM_H */
