#ifndef AT32_ODRIVE_UTIL_H
#define AT32_ODRIVE_UTIL_H

/*
 * Small pure-C helpers shared by the control and communication modules.
 *
 * This header must stay free of CMSIS/target dependencies: the modules that
 * include it are also compiled for the host-side tests in tests/.
 */

/* Float constants, written with the same digits the modules used inline so the
 * replacement is bit-identical to the previous per-file M_PI defines. */
#define UTIL_PI_F            (3.14159265358979323846f)
#define UTIL_INV_SQRT3_F     (0.5773502691896257f)
#define UTIL_SQRT3_OVER_2_F  (0.8660254037844386f)

float util_clampf(float value, float min_value, float max_value);

/* Wrap a value into [0, range).  Returns value unchanged when range <= 0. */
float util_wrap_range(float value, float range);

#endif /* AT32_ODRIVE_UTIL_H */
