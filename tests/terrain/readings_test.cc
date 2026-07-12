#include <moppe/terrain/readings.hh>

#include <tests/test.hh>

#include <array>

using namespace moppe::terrain;

MOPPE_TEST (height_range_is_a_materialized_terrain_reading) {
  const std::array heights { 0.5f, -2.0f, 7.0f, 1.5f };
  const TerrainView terrain (
    { .width = 2, .height = 2, .height_scale = 100.0f * mp_units::si::metre },
    heights);
  const HeightRange range = measure_height_range (terrain);

  MOPPE_CHECK_NEAR (range.minimum, -2.0f, 0.0f);
  MOPPE_CHECK_NEAR (range.maximum, 7.0f, 0.0f);
  static_assert (mp_units::QuantityOf<decltype (terrain.elevation_at (0, 0)),
                                      mp_units::isq::length>);
  static_assert (
    mp_units::QuantityOf<decltype (terrain.relative_elevation_at (0, 0)),
                         relative_elevation>);
  MOPPE_CHECK_NEAR (
    terrain.elevation_at (0, 0).numerical_value_in (mp_units::si::metre),
    50.0f,
    0.0f);
}

MOPPE_TEST (terrain_view_rejects_a_mismatched_raster) {
  const std::array heights { 0.0f, 1.0f, 2.0f };
  bool threw = false;
  try {
    (void)TerrainView ({ .width = 2, .height = 2 }, heights);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}
