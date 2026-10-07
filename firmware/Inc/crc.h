#ifndef AT32_ODRIVE_CRC_H
#define AT32_ODRIVE_CRC_H

#include <stdint.h>

/*
 * CRC-32 (reflected, polynomial 0xEDB88320, initial value 0xFFFFFFFF, final
 * XOR), the same variant used for the persisted configuration image.
 *
 * crc32_update_byte() is the streaming primitive and does NOT apply the final
 * XOR, so a caller that has to skip or mask part of a buffer (as the config
 * image does with its own CRC field) can accumulate byte by byte.
 */

#define CRC32_INITIAL_VALUE   ((uint32_t)0xFFFFFFFFu)

uint32_t crc32_update_byte(uint32_t crc, uint8_t data);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);
uint32_t crc32_compute(const uint8_t *data, uint32_t length);

#endif /* AT32_ODRIVE_CRC_H */
