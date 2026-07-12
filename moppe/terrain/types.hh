#ifndef MOPPE_TERRAIN_TYPES_HH
#define MOPPE_TERRAIN_TYPES_HH

#include <cstdint>

namespace moppe::terrain {
  struct Seed {
    std::uint32_t value;

    friend constexpr bool operator== (Seed, Seed) = default;
  };

  constexpr Seed next_seed (Seed seed) {
    return Seed { seed.value + 1 };
  }

  struct SequenceOffset {
    std::uint64_t value;

    friend constexpr bool operator== (SequenceOffset, SequenceOffset) = default;
  };
}

#endif
