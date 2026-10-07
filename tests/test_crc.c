/*
 * Host-side tests for the CRC-32 used by the persisted configuration image
 * (crc.c).  A wrong CRC either rejects a valid calibration record or, worse,
 * accepts a corrupted one, so the check values are pinned here.
 */
#include "crc.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

static void check_u32(const char *name, uint32_t actual, uint32_t expected)
{
  if (actual != expected)
  {
    printf("FAIL %s: actual=0x%08lX expected=0x%08lX\n",
           name, (unsigned long)actual, (unsigned long)expected);
    g_failures++;
  }
}

int main(void)
{
  static const uint8_t check_data[] = "123456789";
  uint8_t buffer[16];
  uint32_t streamed;

  g_failures = 0;

  /* Standard CRC-32 check value of "123456789". */
  check_u32("crc32.check_value", crc32_compute(check_data, 9u), 0xCBF43926u);

  /* Empty input yields the zero value of the final-XOR variant. */
  check_u32("crc32.empty", crc32_compute(NULL, 0u), 0x00000000u);

  /* The streaming primitive must agree with the one-shot helper. */
  streamed = CRC32_INITIAL_VALUE;
  for (uint32_t i = 0u; i < 9u; i++)
  {
    streamed = crc32_update_byte(streamed, check_data[i]);
  }
  check_u32("crc32.streaming_matches", streamed ^ CRC32_INITIAL_VALUE, 0xCBF43926u);

  /* Same thing through the buffer API. */
  memset(buffer, 0, sizeof(buffer));
  memcpy(buffer, check_data, 9u);
  check_u32("crc32.buffer_matches", crc32_compute(buffer, 9u), 0xCBF43926u);

  /* A single flipped bit must change the result. */
  buffer[3] ^= 0x01u;
  check_u32("crc32.detects_bit_flip", crc32_compute(buffer, 9u) != 0xCBF43926u, 1u);

  /* Length changes the result even with identical leading bytes. */
  check_u32("crc32.length_sensitive", crc32_compute(buffer, 9u) != crc32_compute(buffer, 8u), 1u);

  if (g_failures == 0)
  {
    printf("PASS test_crc\n");
    return 0;
  }

  printf("FAIL test_crc: %d checks failed\n", g_failures);
  return 1;
}
