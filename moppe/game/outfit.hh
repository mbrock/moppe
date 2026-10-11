#ifndef MOPPE_GAME_OUTFIT_HH
#define MOPPE_GAME_OUTFIT_HH

#include <cstdint>

namespace moppe::game {
  // What a hiker wears, so that two of them can be told apart: the blue
  // jacket and red cap the model was painted in, or a green jacket and an
  // orange cap. The cut is the same; only the paints differ
  // (game/figure.cc).
  enum class Outfit : std::uint8_t { blue_jacket, green_jacket };
}

#endif
