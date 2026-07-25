#include <moppe/map/surface.hh>

#include <moppe/game/surface_presentation.hh>

#include <tests/recording_renderer.hh>
#include <tests/surface_fixture.hh>
#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
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

MOPPE_TEST (surface_sections_materialize_typed_height_and_normal_columns) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (4, 4, spatial_extent_in_metres (Vec3 (40, 0, 40))));
  for (int row = 0; row < static_cast<int> (surface.domain ().height ()); ++row)
    for (int column = 0; column < static_cast<int> (surface.domain ().width ());
         ++column)
      spatial::get<terrain::surface_elevation> (surface[terrain::TerrainIndex {
        static_cast<std::size_t> (column), static_cast<std::size_t> (row) }]) =
        moppe::terrain::surface_elevation_point (
          (0.05f *
           static_cast<float> (
             row * static_cast<int> (surface.domain ().width ()) + column)) *
          20.0f * mp_units::si::metre);
  map::rebuild_geometry (surface);

  const auto& geometry = surface;
  static_assert (spatial::FiniteDomain<terrain::TerrainDomain>);
  static_assert (
    spatial::InterpolationDomain<terrain::TerrainDomain, position_t>);
  static_assert (mp_units::QuantityPointOf<
                 decltype (spatial::sample<terrain::surface_elevation> (
                   surface, position (Vec3 ()))),
                 map::surface_elevation>);
  static_assert (
    mp_units::QuantityOf<decltype (spatial::sample<terrain::terrain_normal> (
                           surface, position (Vec3 ()))),
                         terrain::terrain_normal>);
  static_assert (
    mp_units::QuantityOf<decltype (spatial::sample<map::snow_support> (
                           surface, position (Vec3 ()))),
                         map::snow_support>);

  const terrain::TerrainIndex index { 2, 1 };
  const auto elevation =
    spatial::get<terrain::surface_elevation> (geometry[index]);
  const auto normal = spatial::get<terrain::terrain_normal> (geometry[index]);
  const auto snow_support = spatial::get<map::snow_support> (geometry[index]);
  MOPPE_CHECK_NEAR (
    terrain::surface_elevation_value (elevation),
    terrain::surface_elevation_value (
      spatial::get<terrain::surface_elevation> (surface[terrain::TerrainIndex {
        static_cast<std::size_t> (2), static_cast<std::size_t> (1) }])),
    1e-6f);
  check_surface_vector (
    normal_value (normal),
    spatial::get<terrain::terrain_normal> (surface[{ 2, 1 }])
      .numerical_value_in (one));
  MOPPE_CHECK (snow_support >= 0.0f * map::snow_support[one]);
  MOPPE_CHECK (snow_support <= 1.0f * map::snow_support[one]);
}

MOPPE_TEST (snow_support_reads_a_broader_slope_than_the_lighting_normal) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (9, 9, spatial_extent_in_metres (Vec3 (90, 0, 90))));
  for (int row = 0; row < static_cast<int> (surface.domain ().height ()); ++row)
    for (int column = 0; column < static_cast<int> (surface.domain ().width ());
         ++column)
      spatial::get<terrain::surface_elevation> (surface[terrain::TerrainIndex {
        static_cast<std::size_t> (column), static_cast<std::size_t> (row) }]) =
        moppe::terrain::surface_elevation_point ((column < 4 ? 0.2f : 0.8f) *
                                                 80.0f * mp_units::si::metre);
  map::rebuild_geometry (surface);

  const float detailed_up =
    spatial::get<terrain::terrain_normal> (surface[{ 4, 4 }])
      .numerical_value_in (one)[1];
  const float supported_up =
    spatial::sample<map::snow_support> (surface, position (Vec3 (40, 0, 40)))
      .numerical_value_in (one);
  // Column 8 borders the low side across the wrap; probe the flat
  // interior instead.
  const float flat_up =
    spatial::sample<map::snow_support> (surface, position (Vec3 (60, 0, 40)))
      .numerical_value_in (one);
  MOPPE_CHECK (supported_up > detailed_up + 0.05f);
  MOPPE_CHECK (flat_up > 0.99f);
}

MOPPE_TEST (home_base_is_a_distinct_materialized_surface_site) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (3, 3, spatial_extent_in_metres (Vec3 (30, 0, 30))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.2f) * 10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

  const std::array influence { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                               0.0f, 0.0f, 0.0f, 0.0f };
  const std::array trail {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
  };
  const map::SurfaceReadings use = test::complete_readings (
    surface,
    { .use = test::trail_use_map (surface.domain (), trail, influence) });
  MOPPE_CHECK (spatial::sample<map::home_base_influence> (
                 use, position (Vec3 (10, 0, 10))) ==
               1.0f * map::home_base_influence[one]);
  MOPPE_CHECK (spatial::get<map::home_base_influence> (use)[0] ==
               0.0f * map::home_base_influence[one]);
}

MOPPE_TEST (trail_influence_is_a_materialized_surface_mask) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (3, 3, spatial_extent_in_metres (Vec3 (30, 0, 30))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.2f) * 10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

  const std::array influence { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                               0.0f, 0.0f, 0.0f, 0.0f };
  const std::array home {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
  };
  const map::SurfaceReadings use = test::complete_readings (
    surface,
    { .use = test::trail_use_map (surface.domain (), influence, home) });
  MOPPE_CHECK (
    spatial::sample<map::trail_influence> (use, position (Vec3 (10, 0, 10))) ==
    1.0f * map::trail_influence[one]);
  MOPPE_CHECK_NEAR (
    spatial::sample<map::trail_influence> (use, position (Vec3 (5, 0, 10)))
      .numerical_value_in (one),
    0.5f,
    1e-6f);
}

MOPPE_TEST (tree_habitat_is_a_materialized_surface_reading) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (5, 5, spatial_extent_in_metres (Vec3 (50, 0, 50))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.40f) * 200.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

  std::vector<float> moisture (25, 0.48f);
  const position_t center = position (Vec3 (20, 0, 20));
  const map::SurfaceReadings drained = test::complete_readings (
    surface, { .moisture = test::moisture_map (surface.domain (), moisture) });
  MOPPE_CHECK (spatial::sample<map::tree_habitat> (drained, center) >
               0.8f * map::tree_habitat[one]);

  std::fill (moisture.begin (), moisture.end (), 1.0f);
  const map::SurfaceReadings sodden = test::complete_readings (
    surface, { .moisture = test::moisture_map (surface.domain (), moisture) });
  MOPPE_CHECK (spatial::sample<map::tree_habitat> (sodden, center) <
               0.4f * map::tree_habitat[one]);
}

MOPPE_TEST (forest_cover_is_patchy_deterministic_and_respects_clearings) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    65, 65, spatial_extent_in_metres (Vec3 (320, 0, 320))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.42f) * 180.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  const terrain::TerrainDomain& domain = surface.domain ();
  const map::TreeHabitatMap habitat =
    map::analyze_tree_habitat (surface,
                               test::uniform_moisture (domain, 0.48f),
                               50.0f * u::m,
                               160.0f * u::m);
  const terrain::TrailUseMap untravelled =
    test::uniform_use (domain, 0.0f, 0.0f);

  const map::ForestCoverMap covered =
    map::analyze_forest_cover (habitat, untravelled, 0x12345678U);
  const auto& first = spatial::get<map::forest_cover> (covered);
  std::vector<float> values;
  values.reserve (first.size ());
  for (const map::ForestCover value : first)
    values.push_back (value.numerical_value_in (one));
  MOPPE_CHECK (*std::ranges::max_element (values) > 0.65f);
  MOPPE_CHECK (*std::ranges::min_element (values) < 0.05f);

  const map::ForestCoverMap again =
    map::analyze_forest_cover (habitat, untravelled, 0x12345678U);
  const auto& repeated = spatial::get<map::forest_cover> (again);
  for (std::size_t offset = 0; offset < values.size (); ++offset)
    MOPPE_CHECK_NEAR (
      values[offset], repeated[offset].numerical_value_in (one), 1e-7f);

  const map::ForestCoverMap settled = map::analyze_forest_cover (
    habitat, test::uniform_use (domain, 0.0f, 1.0f), 0x12345678U);
  const auto& cleared = spatial::get<map::forest_cover> (settled);
  MOPPE_CHECK (std::ranges::all_of (cleared, [] (map::ForestCover value) {
    return value == 0.0f * map::forest_cover[one];
  }));
}

MOPPE_TEST (surface_reconstruction_matches_authoritative_geometry) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (5, 5, spatial_extent_in_metres (Vec3 (50, 0, 50))));
  for (int row = 0; row < static_cast<int> (surface.domain ().height ()); ++row)
    for (int column = 0; column < static_cast<int> (surface.domain ().width ());
         ++column)
      spatial::get<terrain::surface_elevation> (surface[terrain::TerrainIndex {
        static_cast<std::size_t> (column), static_cast<std::size_t> (row) }]) =
        moppe::terrain::surface_elevation_point (
          (0.03f * column * column + 0.02f * row) * 30.0f *
          mp_units::si::metre);
  map::rebuild_geometry (surface);

  // Reconstruction is the domain's own bilinear stencil. State the expected
  // rule here independently, over the authoritative lattice.
  const Vec3 spacing = Vec3 (
    surface.domain ().spacing_x_m (), 1.0f, surface.domain ().spacing_z_m ());
  const auto expected_height = [&] (float x, float z) {
    const float gx = x / spacing[0];
    const float gz = z / spacing[2];
    const int x0 = static_cast<int> (std::floor (gx));
    const int z0 = static_cast<int> (std::floor (gz));
    const auto corner = [&] (int column, int row) {
      return elevation_value (spatial::get<terrain::surface_elevation> (
        surface[terrain::TerrainIndex {
          static_cast<std::size_t> (terrain::wrap_index (
            column, static_cast<int> (surface.domain ().width ()))),
          static_cast<std::size_t> (terrain::wrap_index (
            row, static_cast<int> (surface.domain ().height ()))) }]));
    };
    const float tx = gx - x0;
    const float tz = gz - z0;
    return std::lerp (
      std::lerp (corner (x0, z0), corner (x0 + 1, z0), tx),
      std::lerp (corner (x0, z0 + 1), corner (x0 + 1, z0 + 1), tx),
      tz);
  };

  const std::array points { Vec3 (0, 0, 0),
                            Vec3 (4.25f, 0, 7.75f),
                            Vec3 (16.4f, 0, 21.8f),
                            Vec3 (29.9f, 0, 29.9f) };
  for (const Vec3& point : points) {
    const position_t p = position (point);
    MOPPE_CHECK_NEAR (
      elevation_value (
        spatial::sample<terrain::surface_elevation> (surface, p)),
      expected_height (point[0], point[2]),
      1e-5f);
    // The plain-metre reconstructions are the same sample, unwrapped.
    MOPPE_CHECK_NEAR (
      elevation_value (
        spatial::sample<terrain::surface_elevation> (surface, p)),
      terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (point[0], 0.0f, point[2])))),
      1e-6f);
    check_surface_vector (
      normal_value (spatial::sample<terrain::terrain_normal> (surface, p)),
      spatial::sample<terrain::terrain_normal> (
        surface, moppe::position (Vec3 (point[0], 0.0f, point[2])))
        .numerical_value_in (mp_units::one),
      1e-6f);
  }
}

MOPPE_TEST (surface_reconstruction_wraps_the_torus) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (5, 5, spatial_extent_in_metres (Vec3 (40, 0, 40))));
  for (int row = 0; row < static_cast<int> (surface.domain ().height ()); ++row)
    for (int column = 0; column < static_cast<int> (surface.domain ().width ());
         ++column)
      spatial::get<terrain::surface_elevation> (surface[terrain::TerrainIndex {
        static_cast<std::size_t> (column), static_cast<std::size_t> (row) }]) =
        moppe::terrain::surface_elevation_point (
          (0.04f * static_cast<float> (row + 2 * column)) * 20.0f *
          mp_units::si::metre);
  map::rebuild_geometry (surface);

  // Sampling one period apart reads the same surface.
  const Vec3 period = Vec3 (meters_value (surface.domain ().period_x ()),
                            0.0f,
                            meters_value (surface.domain ().period_z ()));
  for (const Vec3& point : { Vec3 (3.25f, 0, 7.5f), Vec3 (39.25f, 0, 37.5f) }) {
    const position_t p = position (point);
    const position_t wrapped =
      position (point + Vec3 (period[0], 0.0f, period[2]));
    MOPPE_CHECK_NEAR (
      elevation_value (
        spatial::sample<terrain::surface_elevation> (surface, p)),
      elevation_value (
        spatial::sample<terrain::surface_elevation> (surface, wrapped)),
      1e-5f);
    MOPPE_CHECK_NEAR (
      elevation_value (
        spatial::sample<terrain::surface_elevation> (surface, p)),
      terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (point[0], 0.0f, point[2])))),
      1e-5f);
  }
}

MOPPE_TEST (surface_geometry_is_authoritative_without_a_refresh_barrier) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (3, 3, spatial_extent_in_metres (Vec3 (30, 0, 30))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.2f) * 10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  const position_t p = position (Vec3 (5, 0, 5));
  const float before =
    elevation_value (spatial::sample<terrain::surface_elevation> (surface, p));

  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.7f) * 10.0f * mp_units::si::metre));
  MOPPE_CHECK (
    before !=
    elevation_value (spatial::sample<terrain::surface_elevation> (surface, p)));
  MOPPE_CHECK_NEAR (
    elevation_value (spatial::sample<terrain::surface_elevation> (surface, p)),
    7.0f,
    1e-6f);
}

MOPPE_TEST (surface_presentation_is_the_numeric_bridge_for_typed_sections) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (3, 3, spatial_extent_in_metres (Vec3 (30, 0, 30))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.2f) * 10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

  std::vector<float> trail (9, 0.0f);
  std::vector<float> home (9, 0.0f);
  trail[4] = 0.75f;
  home[4] = 0.25f;
  const map::SurfaceReadings readings = test::complete_readings (
    surface, { .use = test::trail_use_map (surface.domain (), trail, home) });

  game::SurfacePresentation presentation;
  presentation.refresh (surface, readings);

  MOPPE_CHECK (presentation.trails ().size () == surface.domain ().size ());
  MOPPE_CHECK_NEAR (presentation.trails ()[4], 0.75f, 1e-6f);
  MOPPE_CHECK_NEAR (presentation.home_base ()[4], 0.25f, 1e-6f);
  MOPPE_CHECK (presentation.channel_flux ().size () == 18);
  MOPPE_CHECK (presentation.snow_support ().size () == 9);
  MOPPE_CHECK (presentation.forest ().size () == 9);
}

MOPPE_TEST (surface_material_sections_keep_meaning_until_the_numeric_bridge) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (
    terrain::TerrainDomain (3, 3, spatial_extent_in_metres (Vec3 (30, 0, 30))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.2f) * 10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

  std::vector<float> moisture (9, 0.4f);
  std::vector<float> distance (9, 8.0f);
  std::vector<float> eroded (9, 0.0f);
  std::vector<float> deposited (9, 0.0f);
  moisture[4] = 1.0f;
  distance[4] = 2.5f;
  eroded[4] = 2.0f;
  eroded[5] = 4.0f;
  deposited[4] = 3.0f;
  deposited[5] = 6.0f;

  for (std::size_t cell = 0; cell < eroded.size (); ++cell) {
    const terrain::TerrainIndex index { cell % 3, cell / 3 };
    spatial::get<map::eroded_surface_material> (surface[index]) =
      eroded[cell] * map::eroded_surface_material[mp_units::one];
    spatial::get<map::deposited_surface_material> (surface[index]) =
      deposited[cell] * map::deposited_surface_material[mp_units::one];
  }
  const map::SurfaceReadings values = test::complete_readings (
    surface,
    { .moisture = test::moisture_map (surface.domain (), moisture),
      .waterline = test::waterline_map (surface.domain (), distance) });

  static_assert (
    mp_units::QuantityOf<map::SurfaceMoisture, map::surface_moisture>);
  static_assert (
    mp_units::QuantityOf<map::WaterlineDistance, map::waterline_distance>);
  MOPPE_CHECK (spatial::get<map::surface_moisture> (values)[4] ==
               1.0f * map::surface_moisture[one]);
  MOPPE_CHECK (spatial::get<map::waterline_distance> (values)[4] ==
               2.5f * map::waterline_distance[u::m]);
  const position_t center = position (Vec3 (10, 0, 10));
  MOPPE_CHECK (spatial::sample<map::surface_moisture> (values, center) ==
               1.0f * map::surface_moisture[one]);
  MOPPE_CHECK (spatial::sample<map::waterline_distance> (values, center) ==
               2.5f * map::waterline_distance[u::m]);
  MOPPE_CHECK_NEAR (
    spatial::get<map::erosion_exposure> (values)[4].numerical_value_in (one),
    0.5f,
    1e-6f);
  MOPPE_CHECK_NEAR (
    spatial::get<map::deposition_cover> (values)[4].numerical_value_in (one),
    0.5f,
    1e-6f);

  game::SurfacePresentation presentation;
  presentation.refresh (surface, values);
  MOPPE_CHECK_NEAR (presentation.moisture ()[4], 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (presentation.waterline_distance ()[4], 2.5f, 1e-6f);
  MOPPE_CHECK_NEAR (presentation.geology ()[8], 0.5f, 1e-6f);
  MOPPE_CHECK_NEAR (presentation.geology ()[9], 0.5f, 1e-6f);
}
