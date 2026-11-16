#ifndef MOPPE_TERRAIN_TORUS_HH
#define MOPPE_TERRAIN_TORUS_HH

#include <cmath>

// The world is a torus.  These are the wrap operations everything that
// lives on it shares; the period is always the full lattice extent.  A
// player, a vehicle, or a camera is never wrapped back into the world, so
// anything measured against one must be measured by the shortest way
// round (minimum_image_delta), or drawn at the copy of the world nearest
// it (nearest_image).  A period of zero or less means no wrapping.

namespace moppe::terrain {
  inline int wrap_index (int value, int period) {
    const int remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
  }

  inline float wrap_coordinate (float value, float period) {
    const float wrapped = value - std::floor (value / period) * period;
    return wrapped < period ? wrapped : 0.0f;
  }

  // The part of `delta` that is a whole number of laps of the world.
  inline float whole_periods (float delta, float period) {
    return period > 0.0f ? std::round (delta / period) * period : 0.0f;
  }

  // The shortest way across the seam: `delta` brought to within half a
  // period of zero.
  inline float minimum_image_delta (float delta, float period) {
    return delta - whole_periods (delta, period);
  }

  // The lattice's version, in whole cells.
  inline int minimum_image_cells (int delta, int period) {
    if (delta > period / 2)
      delta -= period;
    else if (delta < -period / 2)
      delta += period;
    return delta;
  }

  // `value` at the copy of the world nearest `reference`.
  inline float nearest_image (float value, float reference, float period) {
    return reference + minimum_image_delta (value - reference, period);
  }
}

#endif
