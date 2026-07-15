#include <moppe/map/generate.hh>
#include <moppe/map/surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <type_traits>

namespace {
  float elevation_value (const moppe::map::SurfaceElevation& elevation) {
    return elevation.quantity_from_zero ().numerical_value_in (moppe::u::m);
  }

  moppe::Vec3 normal_value (const moppe::map::SurfaceNormal& normal) {
    return normal.numerical_value_in (mp_units::one);
  }

  void check_surface_vector (const moppe::Vec3& actual,
                             const moppe::Vec3& expected,
                             float tolerance = 1e-6f) {
    MOPPE_CHECK_NEAR (actual[0], expected[0], tolerance);
    MOPPE_CHECK_NEAR (actual[1], expected[1], tolerance);
    MOPPE_CHECK_NEAR (actual[2], expected[2], tolerance);
  }
}

MOPPE_TEST (surface_bundle_materializes_typed_height_and_normal_columns) {
  using namespace moppe;
  map::RandomHeightMap map (
    4, 4, Vec3 (40, 20, 40), 1, terrain::Topology::Bounded);
  for (int row = 0; row < map.height (); ++row)
    for (int column = 0; column < map.width (); ++column)
      map.raw_heights ()[row * map.width () + column] =
        0.05f * static_cast<float> (row * map.width () + column);
  map.recompute_normals ();

  map::Surface surface (map);
  const auto& samples = surface.samples ();
  static_assert (spatial::FiniteDomain<map::SurfaceDomain>);
  static_assert (spatial::InterpolationDomain<map::SurfaceDomain, position_t>);
  static_assert (mp_units::QuantityPointOf<decltype (surface.elevation_at (
                                             position (Vec3 ()))),
                                           map::surface_elevation>);
  static_assert (
    mp_units::QuantityOf<decltype (surface.normal_at (position (Vec3 ()))),
                         map::surface_normal>);

  const map::SurfaceIndex index { 2, 1 };
  const auto elevation = spatial::get<map::surface_elevation> (samples[index]);
  const auto normal = spatial::get<map::surface_normal> (samples[index]);
  const auto habitat = spatial::get<map::tree_habitat> (samples[index]);
  const auto forest = spatial::get<map::forest_cover> (samples[index]);
  const auto trail = spatial::get<map::trail_influence> (samples[index]);
  const auto home_base =
    spatial::get<map::home_base_influence> (samples[index]);
  MOPPE_CHECK_NEAR (
    elevation_value (elevation), map.get (2, 1) * map.scale ()[1], 1e-6f);
  check_surface_vector (normal_value (normal), map.normal (2, 1));
  MOPPE_CHECK (habitat == 0.0f * map::tree_habitat[one]);
  MOPPE_CHECK (forest == 0.0f * map::forest_cover[one]);
  MOPPE_CHECK (trail == 0.0f * map::trail_influence[one]);
  MOPPE_CHECK (home_base == 0.0f * map::home_base_influence[one]);
}

MOPPE_TEST (home_base_is_a_distinct_materialized_surface_site) {
  using namespace moppe;
  map::RandomHeightMap map (
    3, 3, Vec3 (30, 10, 30), 20, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 9, 0.2f);
  map.recompute_normals ();
  map::Surface surface (map);

  const std::array influence { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                               0.0f, 0.0f, 0.0f, 0.0f };
  surface.materialize_home_base_influence (influence);
  MOPPE_CHECK (surface.home_base_influence_at (position (Vec3 (10, 0, 10))) ==
               1.0f * map::home_base_influence[one]);
  MOPPE_CHECK (surface.trail_influence_at (position (Vec3 (10, 0, 10))) ==
               0.0f * map::trail_influence[one]);
}

MOPPE_TEST (trail_influence_is_a_materialized_surface_mask) {
  using namespace moppe;
  map::RandomHeightMap map (
    3, 3, Vec3 (30, 10, 30), 19, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 9, 0.2f);
  map.recompute_normals ();
  map::Surface surface (map);

  const std::array influence { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                               0.0f, 0.0f, 0.0f, 0.0f };
  surface.materialize_trail_influence (influence);
  MOPPE_CHECK (surface.trail_influence_at (position (Vec3 (10, 0, 10))) ==
               1.0f * map::trail_influence[one]);
  MOPPE_CHECK_NEAR (surface.trail_influence_at (position (Vec3 (5, 0, 10)))
                      .numerical_value_in (one),
                    0.5f,
                    1e-6f);
}

MOPPE_TEST (tree_habitat_is_a_materialized_surface_reading) {
  using namespace moppe;
  map::RandomHeightMap map (
    5, 5, Vec3 (50, 200, 50), 8, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 25, 0.40f);
  map.recompute_normals ();
  map::Surface surface (map);

  std::vector<float> moisture (25, 0.48f);
  surface.derive_tree_habitat (moisture, 50.0f * u::m, 160.0f * u::m);
  const position_t center = position (Vec3 (20, 0, 20));
  MOPPE_CHECK (surface.tree_habitat_at (center) >
               0.8f * map::tree_habitat[one]);

  std::fill (moisture.begin (), moisture.end (), 1.0f);
  surface.derive_tree_habitat (moisture, 50.0f * u::m, 160.0f * u::m);
  MOPPE_CHECK (surface.tree_habitat_at (center) <
               0.4f * map::tree_habitat[one]);
}

MOPPE_TEST (forest_cover_is_patchy_deterministic_and_respects_clearings) {
  using namespace moppe;
  map::RandomHeightMap map (
    65, 65, Vec3 (320, 180, 320), 31, terrain::Topology::Torus);
  std::fill (map.raw_heights (), map.raw_heights () + 65 * 65, 0.42f);
  map.recompute_normals ();
  map::Surface surface (map);
  surface.derive_tree_habitat (
    std::vector<float> (65 * 65, 0.48f), 50.0f * u::m, 160.0f * u::m);
  surface.derive_forest_cover (0x12345678U);

  const auto& first = spatial::get<map::forest_cover> (surface.samples ());
  std::vector<float> values;
  values.reserve (first.size ());
  for (const map::ForestCover value : first)
    values.push_back (value.numerical_value_in (one));
  MOPPE_CHECK (*std::ranges::max_element (values) > 0.65f);
  MOPPE_CHECK (*std::ranges::min_element (values) < 0.05f);
  for (std::size_t row = 0; row < 65; ++row)
    MOPPE_CHECK_NEAR (values[row * 65], values[row * 65 + 64], 1e-6f);

  surface.derive_forest_cover (0x12345678U);
  const auto& repeated = spatial::get<map::forest_cover> (surface.samples ());
  for (std::size_t offset = 0; offset < values.size (); ++offset)
    MOPPE_CHECK_NEAR (
      values[offset], repeated[offset].numerical_value_in (one), 1e-7f);

  surface.materialize_home_base_influence (std::vector<float> (65 * 65, 1.0f));
  surface.derive_forest_cover (0x12345678U);
  const auto& cleared = spatial::get<map::forest_cover> (surface.samples ());
  MOPPE_CHECK (std::ranges::all_of (cleared, [] (map::ForestCover value) {
    return value == 0.0f * map::forest_cover[one];
  }));
}

MOPPE_TEST (surface_reconstruction_matches_bounded_heightmap_interpolation) {
  using namespace moppe;
  map::RandomHeightMap map (
    5, 5, Vec3 (50, 30, 50), 2, terrain::Topology::Bounded);
  for (int row = 0; row < map.height (); ++row)
    for (int column = 0; column < map.width (); ++column)
      map.raw_heights ()[row * map.width () + column] =
        0.03f * column * column + 0.02f * row;
  map.recompute_normals ();
  map::Surface surface (map);

  const std::array points { Vec3 (0, 0, 0),
                            Vec3 (4.25f, 0, 7.75f),
                            Vec3 (16.4f, 0, 21.8f),
                            Vec3 (29.9f, 0, 29.9f) };
  for (const Vec3& point : points) {
    const position_t p = position (point);
    MOPPE_CHECK_NEAR (elevation_value (surface.elevation_at (p)),
                      map.interpolated_height (point[0], point[2]),
                      1e-5f);
    check_surface_vector (normal_value (surface.normal_at (p)),
                          map.interpolated_normal (point[0], point[2]),
                          1e-5f);
  }
}

MOPPE_TEST (surface_reconstruction_matches_periodic_seam_interpolation) {
  using namespace moppe;
  map::RandomHeightMap map (
    5, 5, Vec3 (40, 20, 40), 3, terrain::Topology::Torus);
  for (int row = 0; row < map.unique_height (); ++row)
    for (int column = 0; column < map.unique_width (); ++column)
      map.raw_heights ()[row * map.width () + column] =
        0.04f * static_cast<float> (row + 2 * column);
  for (int row = 0; row < map.unique_height (); ++row)
    map.raw_heights ()[row * map.width () + map.width () - 1] =
      map.raw_heights ()[row * map.width ()];
  for (int column = 0; column < map.width (); ++column)
    map.raw_heights ()[(map.height () - 1) * map.width () + column] =
      map.raw_heights ()[column];
  map.recompute_normals ();
  map::Surface surface (map);

  const Vec3 point (39.25f, 0, 37.5f);
  const position_t p = position (point);
  MOPPE_CHECK_NEAR (elevation_value (surface.elevation_at (p)),
                    map.interpolated_height (point[0], point[2]),
                    1e-5f);
  check_surface_vector (normal_value (surface.normal_at (p)),
                        map.interpolated_normal (point[0], point[2]),
                        1e-5f);
}

MOPPE_TEST (surface_refresh_is_an_explicit_materialization_barrier) {
  using namespace moppe;
  map::RandomHeightMap map (
    3, 3, Vec3 (30, 10, 30), 4, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 9, 0.2f);
  map.recompute_normals ();
  map::Surface surface (map);
  const position_t p = position (Vec3 (5, 0, 5));
  const float before = elevation_value (surface.elevation_at (p));

  std::fill (map.raw_heights (), map.raw_heights () + 9, 0.7f);
  map.recompute_normals ();
  MOPPE_CHECK_NEAR (elevation_value (surface.elevation_at (p)), before, 1e-6f);
  surface.refresh (map);
  MOPPE_CHECK_NEAR (elevation_value (surface.elevation_at (p)), 7.0f, 1e-6f);
}
