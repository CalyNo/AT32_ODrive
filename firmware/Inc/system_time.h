#ifndef AT32_ODRIVE_SYSTEM_TIME_H
#define AT32_ODRIVE_SYSTEM_TIME_H

#include <stdint.h>

/*
 * System time base.
 *
 * SysTick provides a 1 ms monotonic tick.  DWT cycle counter provides
 * cycle/microsecond-resolution busy delays for hardware protocols such as the
 * MT6816 SPI bit-banging fallback.
 */
void system_time_init(void);
uint32_t system_millis(void);
uint32_t system_micros(void);
void system_delay_ms(uint32_t ms);
void system_delay_us(uint32_t us);
void system_delay_cycles(uint32_t cycles);

#endif /* AT32_ODRIVE_SYSTEM_TIME_H */
