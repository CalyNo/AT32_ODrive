#ifndef AT32_ODRIVE_ENCODER_H
#define AT32_ODRIVE_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  ENCODER_TYPE_MT6816 = 0,
  ENCODER_TYPE_SPI1_EXTERNAL = 1
} encoder_type_t;

typedef struct
{
  encoder_type_t type;
  int32_t pole_pairs;     /* motor pole pairs used for electrical angle */
  float cpr;              /* counts per mechanical revolution */
  float direction;        /* +1 or -1 */
  float bandwidth;        /* PLL bandwidth [1/s] */
  float pll_kp;
  float pll_ki;
  float pos_estimate;     /* revolutions, continuous */
  float vel_estimate;     /* revolutions/s */
  float pos_estimate_counts;
  float vel_estimate_counts;
  int32_t shadow_count;
  float pos_offset;       /* electrical phase offset [rad] */
  float phase_offset_counts; /* sub-count phase offset */
  float interpolation;
  uint16_t raw;           /* last raw absolute count */
  uint16_t last_raw;
  float electrical_angle; /* radians [0, 2pi) */
  uint32_t error_count;
  bool magnet_ok;
  bool initialized;
  bool index_found;
  bool pos_estimate_valid;
  bool vel_estimate_valid;
} encoder_t;

void encoder_init(encoder_t *enc, encoder_type_t type, float cpr, float direction);
bool encoder_update(encoder_t *enc, float dt);
float encoder_get_electrical_angle(const encoder_t *enc, float pole_pairs);
void encoder_set_linear_count(encoder_t *enc, int32_t count);
void encoder_set_offset(encoder_t *enc, float offset_rad);
void encoder_set_bandwidth(encoder_t *enc, float bandwidth_hz);
uint16_t encoder_mt6816_read_raw(bool *magnet_ok);
void encoder_spi1_init(void);

#endif /* AT32_ODRIVE_ENCODER_H */
