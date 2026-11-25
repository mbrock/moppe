#ifndef MOPPE_GAME_OUTFIT_HH
#define MOPPE_GAME_OUTFIT_HH

#include <cstddef>
#include <cstdint>

namespace moppe::game {
  // What a hiker wears, so that up to eight players can be told apart: the
  // blue jacket and red cap the model was painted in, then seven Nordic
  // outdoor colourways. The cut is the same; only the paints differ
  // (game/figure.cc).
  enum class Outfit : std::uint8_t {
    blue_jacket,
    green_jacket,
    rust_jacket,
    ochre_jacket,
    plum_jacket,
    teal_jacket,
    pine_jacket,
    berry_jacket,
  };

  inline constexpr std::size_t outfit_count = 8;

  // Short colour label for HUD waymarks and the multiplayer menu.
  inline const char* outfit_name (Outfit outfit) noexcept {
    switch (outfit) {
    case Outfit::blue_jacket:
      return "blue";
    case Outfit::green_jacket:
      return "green";
    case Outfit::rust_jacket:
      return "rust";
    case Outfit::ochre_jacket:
      return "ochre";
    case Outfit::plum_jacket:
      return "plum";
    case Outfit::teal_jacket:
      return "teal";
    case Outfit::pine_jacket:
      return "pine";
    case Outfit::berry_jacket:
      return "berry";
    }
    return "blue";
  }
}

#endif
