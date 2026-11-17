// The website's geology (docs/site.md): the default world's land grown on
// a small lattice, as the game grows it, reported after every step.
#ifndef MOPPE_SITE_GEOLOGY_HH
#define MOPPE_SITE_GEOLOGY_HH

#include <moppe/gfx/math.hh>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace moppe::site {
  // The game's default world: 5 km across, its sea at 50 m.
  inline constexpr meters_t land_period = 5000.0f * u::m;
  inline constexpr meters_t land_sea_level = 50.0f * u::m;

  // Heights in metres, `lattice` squared of them, rows from the north
  // (the raster the page and the GPU read); `years` since the land rose.
  using GrowthStep = std::function<void (int step,
                                         int steps,
                                         julian_years_f64_t years,
                                         std::span<const float> heights)>;

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
