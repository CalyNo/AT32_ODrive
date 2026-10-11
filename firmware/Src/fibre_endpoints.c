#include "fibre_endpoints.h"

#include "config.h"
#include "fibre_endpoints_generated.h"
#include "param.h"

#include <stdbool.h>
#include <string.h>

/*
 * Dispatch layer for the generated Fibre 0.1 endpoint tree.
 *
 * The endpoint ids, the JSON descriptor and the id -> s_params[] map all come
 * from fibre_endpoints_generated.h, which is generated from firmware/Src/param.c.
 * That makes the param table the single source of truth: a path, its codec and
 * its write path exist once, so the ASCII protocol, CAN and Fibre cannot drift.
 *
 * The only values handled here are the ones that have no param table row -- the
 * board identity block -- see fibre_identity_read().
 */

/* -------------------------------------------------------------------------
 * Little-endian wire encoding
 * ------------------------------------------------------------------------- */

static uint16_t put_u8(uint8_t *buffer, uint16_t buffer_len, uint8_t value)
{
  if (buffer_len < 1u)
  {
    return 0u;
  }

  buffer[0] = value;
  return 1u;
}

static uint16_t put_u16(uint8_t *buffer, uint16_t buffer_len, uint16_t value)
{
  if (buffer_len < 2u)
  {
    return 0u;
  }

  buffer[0] = (uint8_t)(value & 0xFFu);
  buffer[1] = (uint8_t)((value >> 8) & 0xFFu);
  return 2u;
}

static uint16_t put_u32(uint8_t *buffer, uint16_t buffer_len, uint32_t value)
{
  if (buffer_len < 4u)
  {
    return 0u;
  }

  buffer[0] = (uint8_t)(value & 0xFFu);
  buffer[1] = (uint8_t)((value >> 8) & 0xFFu);
  buffer[2] = (uint8_t)((value >> 16) & 0xFFu);
  buffer[3] = (uint8_t)((value >> 24) & 0xFFu);
  return 4u;
}

static uint16_t put_u64(uint8_t *buffer, uint16_t buffer_len, uint64_t value)
{
  uint32_t i;

  if (buffer_len < 8u)
  {
    return 0u;
  }

  for (i = 0u; i < 8u; i++)
  {
    buffer[i] = (uint8_t)((value >> (8u * i)) & 0xFFu);
  }

  return 8u;
}

static uint16_t put_f32(uint8_t *buffer, uint16_t buffer_len, float value)
{
  uint32_t bits;

  /* memcpy keeps the encoding free of alignment and strict-aliasing games. */
  memcpy(&bits, &value, sizeof(bits));
  return put_u32(buffer, buffer_len, bits);
}

static uint16_t get_u16(const uint8_t *buffer)
{
  return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8));
}

static uint32_t get_u32(const uint8_t *buffer)
{
  return (uint32_t)buffer[0] |
         ((uint32_t)buffer[1] << 8) |
         ((uint32_t)buffer[2] << 16) |
         ((uint32_t)buffer[3] << 24);
}

/* -------------------------------------------------------------------------
 * Board identity (the one hand-written part)
 *
 * These endpoints have no s_params[] row, so the generator emits
 * FIBRE_EP_NOT_PARAM for them and points the descriptor's only uint8/uint64
 * members here.  Their widths are the generated s_fibre_ep_width[] values
 * (1/1/1/1/8), because the host parses the response strictly by the width the
 * descriptor declares -- one extra or missing byte shifts every following field.
 *
 * The three hw_version fields stay 0: this board has no official ODrive
 * product-line identity, and hosts display "unknown board" rather than treating
 * it as an ODrive Pro/S1/Micro.  Deliberate compatibility choice.
 * ------------------------------------------------------------------------- */

#define AT32_UID_WORD0 (*(volatile uint32_t *)0x1FFFF7E8u)
#define AT32_UID_WORD1 (*(volatile uint32_t *)0x1FFFF7ECu)

static uint16_t fibre_identity_read(uint16_t endpoint_id, uint8_t *buffer, uint16_t buffer_len)
{
  switch ((fibre_ep_id_t)endpoint_id)
  {
    case FIBRE_EP_FW_VERSION_REVISION:
      return put_u8(buffer, buffer_len, (uint8_t)FIRMWARE_VERSION_REVISION);

    case FIBRE_EP_HW_VERSION_MAJOR:
    case FIBRE_EP_HW_VERSION_MINOR:
    case FIBRE_EP_HW_VERSION_VARIANT:
      return put_u8(buffer, buffer_len, 0u);

    case FIBRE_EP_SERIAL_NUMBER:
      return put_u64(buffer, buffer_len,
                     ((uint64_t)AT32_UID_WORD0 << 32) | (uint64_t)AT32_UID_WORD1);

    default:
      return 0u;
  }
}

/* -------------------------------------------------------------------------
 * Descriptor
 * ------------------------------------------------------------------------- */

const char *fibre_endpoints_json(void)
{
  return s_fibre_json;
}

uint32_t fibre_endpoints_json_length(void)
{
  /* Emitted by the same generator run as the bytes, so it cannot go stale. */
  return (uint32_t)FIBRE_JSON_LENGTH;
}

/* -------------------------------------------------------------------------
 * Table lookup
 * ------------------------------------------------------------------------- */

/* The s_params[] row backing an endpoint, or NULL when the endpoint has none
 * (identity endpoints, and save_configuration). */
static const param_entry_t *row_for(uint16_t endpoint_id, uint32_t *count)
{
  const param_entry_t *entries = param_table_entries(count);
  uint16_t row;

  if ((endpoint_id == 0u) || (endpoint_id > FIBRE_EP_COUNT))
  {
    return NULL;
  }

  row = s_fibre_ep_index[endpoint_id];
  if ((row == FIBRE_EP_NOT_PARAM) || ((uint32_t)row >= *count))
  {
    return NULL;
  }

  return &entries[(uint32_t)row];
}

/* -------------------------------------------------------------------------
 * Property read / write
 * ------------------------------------------------------------------------- */

/*
 * Wire width of a property kind, i.e. the width of the codec the descriptor
 * declares for it: 4 bytes for float/int32/uint32, 2 for uint16, 0 for a kind
 * this layer cannot decode.  property_decode() cross-checks it against the
 * generated s_fibre_ep_width[] before decoding.
 */
static uint16_t param_kind_width(param_kind_t kind)
{
  switch (kind)
  {
    case PARAM_KIND_F32:
    case PARAM_KIND_I32:
    case PARAM_KIND_U32:
      return 4u;

    case PARAM_KIND_U16:
      return 2u;

    default:
      return 0u;
  }
}

uint16_t fibre_endpoint_read(uint16_t endpoint_id, uint8_t *buffer, uint16_t buffer_len)
{
  uint32_t count = 0u;
  const param_entry_t *entry;
  param_value_t value;

  if ((endpoint_id == 0u) || (endpoint_id > FIBRE_EP_COUNT))
  {
    return 0u;
  }

  /*
   * Dispatch on the kind, not on whether a table row exists: axis0.clear_errors
   * keeps a real row (its on_write hook is the only clear-errors implementation)
   * but it is a function, so it must not be served as a property.
   */
  switch ((fibre_ep_kind_t)s_fibre_ep_kind[endpoint_id])
  {
    case FIBRE_EP_KIND_IDENTITY:
      return fibre_identity_read(endpoint_id, buffer, buffer_len);

    case FIBRE_EP_KIND_FUNCTION:
      return 0u; /* a function declares no codec, so it has no value */

    case FIBRE_EP_KIND_PROPERTY:
    default:
      break;
  }

  entry = row_for(endpoint_id, &count);
  if (entry == NULL)
  {
    return 0u;
  }

  if (param_read_entry(entry, &value) != PARAM_RESULT_OK)
  {
    /* No read_ptr (write-only row) or an unhandled kind. */
    return 0u;
  }

  /* Widths match the descriptor's declared codec: 4 bytes for F32/I32/U32 and
   * 2 for U16.  s_fibre_ep_width[] carries the same numbers and is what the
   * conformance test cross-checks. */
  switch (value.kind)
  {
    case PARAM_KIND_F32:
      return put_f32(buffer, buffer_len, value.f32);

    case PARAM_KIND_I32:
      return put_u32(buffer, buffer_len, (uint32_t)value.i32);

    case PARAM_KIND_U32:
      return put_u32(buffer, buffer_len, value.u32);

    case PARAM_KIND_U16:
      return put_u16(buffer, buffer_len, value.u16);

    default:
      return 0u;
  }
}

/*
 * Decode one property payload into the float that param_write_entry() takes,
 * or return false when the payload cannot be written.
 *
 * Two independent reasons to refuse:
 *
 *  - The generated width (s_fibre_ep_width[], from the descriptor's declared
 *    codec) must equal the width implied by entry->kind, which is what the
 *    switch below actually decodes with.  Both descend from the same param.c
 *    row, but descending from one row is a coupling, not a guarantee, and a
 *    disagreement would make the 4-byte decode read two bytes past a 2-byte
 *    payload.  Checked, not assumed.
 *
 *  - The payload length must be exactly that width: the host sends an EMPTY
 *    payload when it only wants to read, and an empty payload must never be
 *    read as "write zero".  A wrong length is not written at all.
 */
static bool property_decode(const param_entry_t *entry, uint16_t endpoint_id,
                            const uint8_t *buffer, uint16_t buffer_len, float *value_out)
{
  /* Generated from the declared codec. */
  uint16_t width = (uint16_t)s_fibre_ep_width[endpoint_id];

  if (width != param_kind_width(entry->kind))
  {
    return false;
  }

  if (buffer_len != width)
  {
    return false;
  }

  switch (entry->kind)
  {
    case PARAM_KIND_F32:
      /* IEEE-754 single, little-endian, so the bits can be copied directly. */
      memcpy(value_out, buffer, sizeof(*value_out));
      break;

    case PARAM_KIND_I32:
      *value_out = (float)(int32_t)get_u32(buffer);
      break;

    case PARAM_KIND_U32:
      *value_out = (float)get_u32(buffer);
      break;

    case PARAM_KIND_U16:
      *value_out = (float)get_u16(buffer);
      break;

    default:
      return false;
  }

  return true;
}

/* -------------------------------------------------------------------------
 * Zero-argument function dispatch
 * ------------------------------------------------------------------------- */

static uint16_t fibre_function_call(uint16_t endpoint_id, uint16_t payload_length,
                                    const param_entry_t *entry)
{
  /* Zero-argument functions: the host sends no payload and expects no value. */
  if (payload_length != 0u)
  {
    return 0u;
  }

  switch ((fibre_ep_id_t)endpoint_id)
  {
    case FIBRE_EP_SAVE_CONFIGURATION:
      /* The same entry point the ASCII 'ss' alias and the GUI both use. */
      (void)param_save_configuration();
      return 0u;

    case FIBRE_EP_AXIS0_CLEAR_ERRORS:
      /*
       * Do NOT add a second clear-errors implementation.  The real one is
       * param_on_write_clear_errors(), which is `static` in Src/param.c and is
       * reached through the `axis0.clear_errors` row's on_write hook.  The
       * generator therefore points s_fibre_ep_index[] at that row and we simply
       * write it -- identical to what the UART 'sc' command does, including the
       * return-to-IDLE behaviour.
       */
      (void)param_write_entry(entry, 1.0f);
      return 0u;

    default:
      return 0u;
  }
}

uint16_t fibre_endpoint_write(uint16_t endpoint_id, const uint8_t *buffer, uint16_t buffer_len)
{
  uint32_t count = 0u;
  const param_entry_t *entry;
  float value = 0.0f;

  if ((endpoint_id == 0u) || (endpoint_id > FIBRE_EP_COUNT))
  {
    return 0u;
  }

  /*
   * Dispatch on the kind, not on whether a table row exists: a function
   * endpoint has a row too (that is how clear_errors reaches the shared write
   * path), but it must be called with an EMPTY payload instead of being written
   * with a codec-width value.
   */
  switch ((fibre_ep_kind_t)s_fibre_ep_kind[endpoint_id])
  {
    case FIBRE_EP_KIND_FUNCTION:
      return fibre_function_call(endpoint_id, buffer_len, row_for(endpoint_id, &count));

    case FIBRE_EP_KIND_IDENTITY:
      return 0u; /* identity endpoints are read-only */

    case FIBRE_EP_KIND_PROPERTY:
    default:
      break;
  }

  entry = row_for(endpoint_id, &count);
  if (entry == NULL)
  {
    return 0u;
  }

  if (!property_decode(entry, endpoint_id, buffer, buffer_len, &value))
  {
    return 0u;
  }

  /* param_write_entry() owns every bound check, the nvm_config_apply() mirror
   * and the calibration-invalidating side effects -- none of that is repeated
   * here. */
  if (param_write_entry(entry, value) != PARAM_RESULT_OK)
  {
    return 0u;
  }

  /* Consumed exactly the payload that was offered, which is the codec width. */
  return buffer_len;
}
