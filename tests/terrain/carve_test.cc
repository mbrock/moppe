#include <moppe/terrain/carve.hh>

#include <tests/test.hh>

#include <cmath>
#include <cstddef>
#include <vector>

using namespace moppe::terrain;

namespace {
  // A V-valley descending toward an ocean row: every column drains into
  // the center column, which carries a visible river to the sea.
  std::vector<float> valley_to_sea () {
    std::vector<float> heights (9 * 9);
    for (int y = 0; y < 9; ++y)
      for (int x = 0; x < 9; ++x)
        heights[static_cast<std::size_t> (y) * 9 + x] =
          y == 8 ? -0.1f : 0.6f - 0.06f * y + 0.03f * std::abs (x - 4);
    return heights;
  }

  TerrainGrid valley_grid () {
    return { .width = 9,
             .height = 9,
             .spacing_x = 5.0f * mp_units::si::metre,
             .spacing_y = 5.0f * mp_units::si::metre,
             .height_scale = 100.0f * mp_units::si::metre };
  }

  constexpr ChannelCarving valley_parameters { .sea_level = 0.0f,
                                               .minimum_area_cells = 4.0f };
}

MOPPE_TEST (carving_cuts_a_monotone_bed_along_the_trunk_river) {
  const std::vector<float> original = valley_to_sea ();
  const TerrainView terrain (valley_grid (), original);
  const ChannelCarvingResult result =
    carve_channels (terrain, valley_parameters);

  MOPPE_CHECK (result.heights.size () == original.size ());
  MOPPE_CHECK (result.report.reaches >= reach_count (1));
  MOPPE_CHECK (result.report.carved_cells > cell_count (0));
  MOPPE_CHECK (result.report.lowered_volume > 0.0);

  for (std::size_t i = 0; i < original.size (); ++i)
    MOPPE_CHECK (result.heights[i] <= original[i] + 1e-7f);

  // The trunk is deepened and its carved bed never rises downstream.
  for (int y = 3; y <= 7; ++y) {
    const std::size_t cell = static_cast<std::size_t> (y) * 9 + 4;
    MOPPE_CHECK (result.heights[cell] < original[cell] - 1e-4f);
    if (y > 3)
      MOPPE_CHECK (result.heights[cell] <= result.heights[cell - 9] + 1e-7f);
  }
}

MOPPE_TEST (carving_is_deterministic) {
  const std::vector<float> original = valley_to_sea ();
  const TerrainView terrain (valley_grid (), original);
  const ChannelCarvingResult first =
    carve_channels (terrain, valley_parameters);
  const ChannelCarvingResult second =
    carve_channels (terrain, valley_parameters);

  MOPPE_CHECK (first.heights == second.heights);
}

MOPPE_TEST (carving_never_raises_a_periodic_terrain) {
  // A wrapped cone draining to one minimum; the duplicated seam row and
  // column repeat the first samples.
  std::vector<float> heights (6 * 6);
  for (int y = 0; y < 6; ++y)
    for (int x = 0; x < 6; ++x) {
      const int ux = x % 5;
      const int uy = y % 5;
      const int dx = std::min (std::abs (ux - 2), 5 - std::abs (ux - 2));
      const int dy = std::min (std::abs (uy - 2), 5 - std::abs (uy - 2));
      heights[static_cast<std::size_t> (y) * 6 + x] =
        0.1f * (dx + dy) + 0.001f * ux;
    }
  const TerrainView terrain ({ .width = 6,
                               .height = 6,
                               .spacing_x = 5.0f * mp_units::si::metre,
                               .spacing_y = 5.0f * mp_units::si::metre,
                               .height_scale = 100.0f * mp_units::si::metre,
                               .topology = Topology::Torus },
                             heights);
  const ChannelCarvingResult result = carve_channels (
    terrain, { .sea_level = -1.0f, .minimum_area_cells = 2.0f });

  MOPPE_CHECK (result.heights.size () == 25);
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 5; ++x)
      MOPPE_CHECK (result.heights[static_cast<std::size_t> (y) * 5 + x] <=
                   heights[static_cast<std::size_t> (y) * 6 + x] + 1e-7f);
}
