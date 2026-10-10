#include "crc.h"

#include <stddef.h>

#define CRC32_POLY_REFLECTED   ((uint32_t)0xEDB88320u)
#define CRC16_POLY             ((uint16_t)0x3D65u)

uint32_t crc32_update_byte(uint32_t crc, uint8_t data)
{
  crc ^= (uint32_t)data;

  for (uint32_t bit = 0u; bit < 8u; bit++)
  {
    if ((crc & 1u) != 0u)
    {
      crc = (crc >> 1) ^ CRC32_POLY_REFLECTED;
    }
    else
    {
      crc >>= 1;
    }
  }

  return crc;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
  if (data == NULL)
  {
    return crc;
  }

  for (uint32_t i = 0u; i < length; i++)
  {
    crc = crc32_update_byte(crc, data[i]);
  }

  return crc;
}

uint32_t crc32_compute(const uint8_t *data, uint32_t length)
{
  return crc32_update(CRC32_INITIAL_VALUE, data, length) ^ CRC32_INITIAL_VALUE;
}

uint16_t crc16_update_byte(uint16_t crc, uint8_t data)
{
  crc ^= (uint16_t)((uint16_t)data << 8);

  for (uint32_t bit = 0u; bit < 8u; bit++)
  {
    if ((crc & 0x8000u) != 0u)
    {
      crc = (uint16_t)((uint16_t)(crc << 1) ^ CRC16_POLY);
    }
    else
    {
      crc = (uint16_t)(crc << 1);
    }
  }

  return crc;
}

uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint32_t length)
{
  if (data == NULL)
  {
    return crc;
  }

  for (uint32_t i = 0u; i < length; i++)
  {
    crc = crc16_update_byte(crc, data[i]);
  }

  return crc;
}
