#include <moppe/terrain/readings.hh>

#include <tests/test.hh>

#include <array>

using namespace moppe::terrain;

MOPPE_TEST (height_range_is_a_materialized_terrain_reading) {
  const std::array heights { 0.5f, -2.0f, 7.0f, 1.5f };
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (2, 2), heights);
  const HeightRange range = measure_height_range (terrain);

  MOPPE_CHECK_NEAR (range.minimum, -2.0f, 0.0f);
  MOPPE_CHECK_NEAR (range.maximum, 7.0f, 0.0f);
  const SurfaceElevation elevation =
    spatial::get<surface_elevation> (terrain).front ();
  static_assert (mp_units::QuantityPoint<decltype (elevation)>);
  MOPPE_CHECK_NEAR (surface_elevation_value (elevation), 0.5f, 0.0f);
}

MOPPE_TEST (elevation_map_rejects_mismatched_samples) {
  const std::array heights { 0.0f, 1.0f, 2.0f };
  bool threw = false;
  try {
    (void)make_elevation_map (TerrainDomain (2, 2), heights);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}
