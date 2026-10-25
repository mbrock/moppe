// The website's geology (docs/site.md): the default world's land grown on
// a small lattice, as the game grows it, reported after every step.
#ifndef MOPPE_SITE_GEOLOGY_HH
#define MOPPE_SITE_GEOLOGY_HH

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace moppe::site {
  // The game's default world: 5 km across, its sea at 50 m.
  inline constexpr float land_period_m = 5000.0f;
  inline constexpr float land_sea_level_m = 50.0f;

  // Heights in metres, `lattice` squared of them, rows from the north.
  using GrowthStep = std::function<void (
    int step, int steps, double years, std::span<const float> heights)>;

  // Raises the land of world `seed` and evolves it through the play
  // profile's two million years, calling `step` with the first heights
  // (step 0) and after each geological step.
  void grow_land (int lattice, std::uint32_t seed, const GrowthStep& step);

  // The heights as the map sheet's shaded relief, `size` pixels square:
  // eight bits each of red, green, blue, and an opaque alpha.
  void paint_relief (std::span<const float> heights,
                     int lattice,
                     int size,
                     std::vector<std::uint8_t>& rgba);
}

#endif
