#include "util.h"

#include <math.h>

float util_clampf(float value, float min_value, float max_value)
{
  if (value > max_value)
  {
    return max_value;
  }
  if (value < min_value)
  {
    return min_value;
  }
  return value;
}

float util_wrap_range(float value, float range)
{
  if ((range <= 0.0f) || (value == 0.0f))
  {
    return value;
  }

  value = fmodf(value, range);
  if (value < 0.0f)
  {
    value += range;
  }
  return value;
}
