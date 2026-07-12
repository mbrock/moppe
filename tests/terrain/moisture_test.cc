#include <moppe/terrain/moisture.hh>

#include <tests/test.hh>

#include <vector>

using namespace moppe::terrain;

MOPPE_TEST (moisture_decays_away_from_standing_water) {
  // A 9x9 plain with one pond cell in the center.
  const std::size_t count = 81;
  const TerrainGrid grid { .width = 9,
                           .height = 9,
                           .spacing_x = 10.0f * mp_units::si::metre,
                           .spacing_y = 10.0f * mp_units::si::metre,
                           .height_scale = 100.0f * mp_units::si::metre };
  const RecipeDomain2D domain { .width = 9, .height = 9 };
  std::vector<WaterBodyId> body (count, LakeCensus::dry);
  body[40] = 0;
  const FloodField flood {
    .source_grid = grid,
    .sea_level = -1.0f,
    .has_ocean = false,
    .water_level = ScalarRaster (domain, std::vector<float> (count, 0.0f)),
    .water_depth = ScalarRaster (domain, std::vector<float> (count, 0.0f)),
    .ocean = std::vector<std::uint8_t> (count, 0),
    .spill_receiver = std::vector<CellIndex> (count, 40),
    .outlets = { 40 }
  };
  const LakeCensus census { .body = std::move (body) };
  const DrainageGraph drainage {
    .source_grid = grid,
    .receiver = std::vector<CellIndex> (count, 40),
    .slope =
      SlopeRaster (ScalarRaster (domain, std::vector<float> (count, 0.01f))),
    .contributing_area = ContributingAreaRaster (
      ScalarRaster (domain, std::vector<float> (count, 100.0f))),
    .basin = std::vector<CellIndex> (count, 0),
    .sinks = { 40 }
  };

  const ScalarRaster moisture = analyze_moisture (flood, census, drainage);

  MOPPE_CHECK (moisture.at (4, 4) > 0.7f);
  MOPPE_CHECK (moisture.at (5, 4) > moisture.at (8, 4));
  MOPPE_CHECK (moisture.at (0, 0) < moisture.at (3, 3));
  for (std::size_t i = 0; i < count; ++i) {
    MOPPE_CHECK (moisture.values ()[i] >= 0.0f);
    MOPPE_CHECK (moisture.values ()[i] <= 1.0f);
  }
}
