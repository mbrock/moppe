#include <moppe/terrain/noise.hh>

#include <algorithm>
#include <array>
#include <random>

namespace moppe::terrain {
  namespace {
    std::uint32_t bounded_random (std::mt19937& rng,
				  std::uint32_t bound) {
      // Match libc++'s unbiased bit-mask rejection, which the original
      // macOS generator used.  Keeping it explicit also makes geological
      // seeds independent of the standard-library implementation.
      std::uint32_t mask = bound - 1;
      mask |= mask >> 1;
      mask |= mask >> 2;
      mask |= mask >> 4;
      mask |= mask >> 8;
      mask |= mask >> 16;
      std::uint32_t value;
      do
	value = rng () & mask;
      while (value >= bound);
      return value;
    }
  }

  PerlinPermutation make_perlin_permutation (std::uint32_t seed) {
    std::mt19937 rng (seed);
    std::array<std::int32_t, 256> shuffled;
    for (std::int32_t i = 0; i < 256; ++i)
      shuffled[static_cast<std::size_t> (i)] = i;
    for (int i = 255; i > 0; --i) {
      const int j = static_cast<int>
	(bounded_random (rng, static_cast<std::uint32_t> (i + 1)));
      std::swap (shuffled[static_cast<std::size_t> (i)],
		 shuffled[static_cast<std::size_t> (j)]);
    }

    PerlinPermutation result;
    for (int i = 0; i < 512; ++i)
      result[static_cast<std::size_t> (i)] =
	shuffled[static_cast<std::size_t> (i & 255)];
    return result;
  }
}
