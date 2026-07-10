#ifndef MOPPE_TERRAIN_NOISE_HH
#define MOPPE_TERRAIN_NOISE_HH

#include <array>
#include <cstdint>

namespace moppe::terrain {
  using PerlinPermutation = std::array<std::int32_t, 512>;

  // Produces the canonical permutation table shared by evaluator backends.
  // Keeping this host-side step common makes CPU and GPU noise differ only
  // in floating-point evaluation, not in their seeded lattice.
  PerlinPermutation make_perlin_permutation (std::uint32_t seed);
}

#endif
