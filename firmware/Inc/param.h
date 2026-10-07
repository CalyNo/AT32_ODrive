#ifndef AT32_ODRIVE_PARAM_H
#define AT32_ODRIVE_PARAM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Single source of truth for the ODrive-style parameter namespace.
 *
 * The UART ASCII protocol ("r <path>" / "w <path> <value>") and the CAN Simple
 * command handlers resolve their parameter access through this table, so that
 * every path is spelled, range-checked, stored and mirrored in exactly one
 * place.
 *
 * Persisted parameters live in g_odrive_config.  After such a write the table
 * calls nvm_config_apply(), which is the only function that copies configuration
 * into the live copies inside g_axis, so the two views cannot drift.
 */

typedef enum
{
  PARAM_KIND_F32 = 0,   /* value printed as float */
  PARAM_KIND_I32,       /* value printed as int32_t */
  PARAM_KIND_U32,       /* value printed as uint32_t */
  PARAM_KIND_U16        /* value printed as uint16_t (e.g. encoder.raw) */
} param_kind_t;

typedef enum
{
  PARAM_RESULT_OK = 0,
  PARAM_RESULT_UNKNOWN,        /* no such path */
  PARAM_RESULT_READ_ONLY,      /* known path, but not writable */
  PARAM_RESULT_OUT_OF_RANGE,   /* value rejected by the row's bounds */
  PARAM_RESULT_MISSING_VALUE   /* 'w' without a value */
} param_result_t;

struct param_entry;

/*
 * Extra handling for rows that cannot be expressed as "store into a config
 * field".  Called after the value has been range checked and stored.
 */
typedef void (*param_on_write_t)(const struct param_entry *entry, float value);

typedef struct param_entry
{
  const char       *path;       /* canonical ASCII path */
  const char       *alias;      /* accepted legacy path, or NULL */
  param_kind_t      kind;       /* representation used when reading */
  uint8_t           precision;  /* decimals for PARAM_KIND_F32 */
  const void       *read_ptr;   /* value printed by param_print(), NULL = unknown */
  void             *config_ptr; /* g_odrive_config field, NULL = runtime-only */
  param_on_write_t  on_write;   /* extra handling, or NULL */
  float             min_value;
  float             max_value;
  bool              clamp;      /* true: clamp to bounds, false: reject */
} param_entry_t;

/* Print "<path>: <value>" (or "<path>: unknown") to the UART. */
void param_print(const char *path);

/* Apply an ASCII write from the UART protocol. */
param_result_t param_write(const char *path, const char *value_text);

/* True for the aliases that store the configuration to flash. */
bool param_is_save_path(const char *path);

/* Persist g_odrive_config (including the runtime calibration results). */
bool param_save_configuration(void);

/*
 * Apply a numeric write by path.  Used by the CAN Simple handlers so that both
 * protocols mutate the same configuration view.  Returns the same result codes
 * as param_write().
 */
param_result_t param_write_value(const char *path, float value);

#endif /* AT32_ODRIVE_PARAM_H */
