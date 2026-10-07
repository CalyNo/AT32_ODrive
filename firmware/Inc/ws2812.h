#ifndef AT32_ODRIVE_WS2812_H
#define AT32_ODRIVE_WS2812_H

#include <stdint.h>

void ws2812_init(void);
void ws2812_set_rgb(uint8_t r, uint8_t g, uint8_t b);
void ws2812_update(void);

#endif /* AT32_ODRIVE_WS2812_H */
