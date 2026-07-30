#include <moppe/terrain/moisture.hh>

#include <tests/test.hh>

#include <vector>

using namespace moppe::terrain;

MOPPE_TEST (moisture_decays_away_from_standing_water) {
  // A 9x9 plain with one pond cell in the center.
  const std::size_t count = 81;
  const TerrainDomain grid (
    9, 9, 10.0f * mp_units::si::metre, 10.0f * mp_units::si::metre);
  const std::vector<float> levels (count, 0.0f);
  const std::vector<float> depths (count, 0.0f);
  const std::vector<float> slopes (count, 0.01f);
  const std::vector<float> areas (count, 100.0f);
  std::vector<WaterBodyId> body_at_cell (count, WaterBodyMembership::dry);
  body_at_cell[40] = 0;
  const FloodField flood { .surface = make_flood_surface (grid, levels, depths),
                           .sea_level = -1.0f,
                           .has_ocean = false,
                           .ocean = std::vector<std::uint8_t> (count, 0),
                           .spill_receiver =
                             std::vector<CellIndex> (count, 40) };
  const WaterBodyMembership water_bodies (std::move (body_at_cell),
                                          WaterBodyDomain (1));
  const DrainageGraph drainage { .readings =
                                   make_drainage_readings (grid, slopes, areas),
                                 .receiver =
                                   std::vector<CellIndex> (count, 40) };

  const MoistureMap moisture = analyze_moisture (flood, water_bodies, drainage);
  const auto& values = spatial::get<surface_moisture> (moisture);
  const auto at = [&] (std::size_t x, std::size_t y) {
    return values[grid.offset ({ x, y })].numerical_value_in (mp_units::one);
  };

  MOPPE_CHECK (at (4, 4) > 0.7f);
  MOPPE_CHECK (at (5, 4) > at (8, 4));
  MOPPE_CHECK (at (0, 0) < at (3, 3));
  for (std::size_t i = 0; i < count; ++i) {
    MOPPE_CHECK (values[i] >= 0.0f * surface_moisture[mp_units::one]);
    MOPPE_CHECK (values[i] <= 1.0f * surface_moisture[mp_units::one]);
  }
}
