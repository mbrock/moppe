#ifndef MOPPE_TERRAIN_TOPOLOGY_HH
#define MOPPE_TERRAIN_TOPOLOGY_HH

#include <cmath>

namespace moppe::terrain {
  enum class Topology { Bounded, Torus };

  inline int wrap_index (int value, int period) {
    const int remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
  }

  inline float wrap_coordinate (float value, float period) {
    const float wrapped = value - std::floor (value / period) * period;
    return wrapped < period ? wrapped : 0.0f;
  }

  inline float minimum_image_delta (float delta, float period) {
    return delta - std::round (delta / period) * period;
  }

  inline float nearest_image (float value, float reference, float period) {
    return reference + minimum_image_delta (value - reference, period);
  }
}

#endif
