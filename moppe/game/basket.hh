#ifndef MOPPE_GAME_BASKET_HH
#define MOPPE_GAME_BASKET_HH

#include <array>
#include <cstdint>
#include <vector>

namespace moppe::game {
  // The autumn woods' mushrooms. Each lives with a tree -- they are the
  // fruit of the fungus wrapped around its roots -- so the kind follows
  // the tree: chanterelles and funnel chanterelles in the moss under
  // spruce, porcini under spruce and birch alike, and fly agaric, red and
  // white-spotted, under birch.
  enum class MushroomKind : std::uint8_t {
    chanterelle,
    funnel_chanterelle,
    porcini,
    fly_agaric,
  };
  inline constexpr int mushroom_kind_count = 4;

  // What was gathered: the plan's indices of the picked mushrooms, in the
  // order they were picked, and how many of each kind lie in the basket.
  struct Basket {
    std::vector<std::uint32_t> picked;
    std::array<int, mushroom_kind_count> count {};
    // Simulation time of the last reach for a mushroom, for its little
    // celebration, and what it was; a refused one was poisonous and was
    // left growing.
    double last_reach_time = -100.0;
    MushroomKind last_kind = MushroomKind::chanterelle;
    bool last_refused = false;

    int total () const {
      int sum = 0;
      for (const int n : count)
        sum += n;
      return sum;
    }
  };
}

#endif
