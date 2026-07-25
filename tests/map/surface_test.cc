#include <moppe/map/surface.hh>

#include <moppe/game/surface_presentation.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/waterline.hh>

#include <tests/recording_renderer.hh>
#include <tests/test.hh>

#include <algorithm>
#include <array>
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

  moppe::terrain::MoistureMap
  moisture_map (const moppe::map::SurfaceDomain& domain,
                std::span<const float> samples) {
    std::vector<moppe::terrain::SurfaceMoisture> values;
    values.reserve (samples.size ());
    for (float sample : samples)
      values.push_back (sample *
                        moppe::terrain::surface_moisture[mp_units::one]);
    return moppe::terrain::MoistureMap (domain, std::move (values));
  }

  moppe::terrain::WaterlineProximity
  waterline_map (const moppe::map::SurfaceDomain& domain,
                 std::span<const float> samples) {
    std::vector<moppe::terrain::WaterlineDistance> values;
    values.reserve (samples.size ());
    for (float sample : samples)
      values.push_back (sample *
                        moppe::terrain::waterline_distance[moppe::u::m]);
    return moppe::terrain::WaterlineProximity (domain, std::move (values));
  }

  moppe::terrain::TrailUseMap
  trail_use_map (const moppe::map::SurfaceDomain& domain,
                 std::span<const float> trails,
                 std::span<const float> home_base) {
    std::vector<moppe::terrain::TrailInfluence> trail_values;
    std::vector<moppe::terrain::HomeBaseInfluence> home_values;
    trail_values.reserve (trails.size ());
    home_values.reserve (home_base.size ());
    for (float value : trails)
      trail_values.push_back (value *
                              moppe::terrain::trail_influence[mp_units::one]);
    for (float value : home_base)
      home_values.push_back (
        value * moppe::terrain::home_base_influence[mp_units::one]);
    return moppe::terrain::TrailUseMap (
      domain, std::move (trail_values), std::move (home_values));
  }
}

MOPPE_TEST (surface_sections_materialize_typed_height_and_normal_columns) {
  using namespace moppe;
  map::Surface map (4, 4, Vec3 (40, 20, 40));
  for (int row = 0; row < map.height (); ++row)
    for (int column = 0; column < map.width (); ++column)
      map.set_elevation (
        column,
        row,
        moppe::terrain::surface_elevation_point (
          (0.05f * static_cast<float> (row * map.width () + column)) * 20.0f *
          mp_units::si::metre));
  map.rebuild_geometry_readings ();

  map::Surface& surface = map;
  const map::SurfaceAtlas& atlas = surface.atlas ();
  const auto& geometry = atlas.geometry ();
  static_assert (spatial::FiniteDomain<map::SurfaceDomain>);
  static_assert (spatial::InterpolationDomain<map::SurfaceDomain, position_t>);
  static_assert (mp_units::QuantityPointOf<decltype (surface.elevation_at (
                                             position (Vec3 ()))),
                                           map::surface_elevation>);
  static_assert (
    mp_units::QuantityOf<decltype (surface.normal_at (position (Vec3 ()))),
                         terrain::terrain_normal>);
  static_assert (mp_units::QuantityOf<decltype (surface.snow_support_at (
                                        position (Vec3 ()))),
                                      map::snow_support>);

  const map::SurfaceIndex index { 2, 1 };
  const auto elevation =
    spatial::get<terrain::surface_elevation> (geometry[index]);
  const auto normal = spatial::get<terrain::terrain_normal> (geometry[index]);
  const auto snow_support = spatial::get<map::snow_support> (geometry[index]);
  MOPPE_CHECK_NEAR (terrain::surface_elevation_value (elevation),
                    terrain::surface_elevation_value (map.elevation_at (2, 1)),
                    1e-6f);
  check_surface_vector (normal_value (normal), map.normal_at (2, 1));
  MOPPE_CHECK (snow_support >= 0.0f * map::snow_support[one]);
  MOPPE_CHECK (snow_support <= 1.0f * map::snow_support[one]);
  MOPPE_CHECK (!atlas.hydrology ().channel_flux ());
  MOPPE_CHECK (!atlas.hydrology ().moisture ());
  MOPPE_CHECK (!atlas.hydrology ().waterline ());
  MOPPE_CHECK (!atlas.geology ().materials ());
  MOPPE_CHECK (!atlas.ecology ().tree_habitat ());
  MOPPE_CHECK (!atlas.ecology ().forest_cover ());
  MOPPE_CHECK (!atlas.use ().readings ());
}

MOPPE_TEST (snow_support_reads_a_broader_slope_than_the_lighting_normal) {
  using namespace moppe;
  map::Surface map (9, 9, Vec3 (90, 80, 90));
  for (int row = 0; row < map.height (); ++row)
    for (int column = 0; column < map.width (); ++column)
      map.set_elevation (
        column,
        row,
        moppe::terrain::surface_elevation_point ((column < 4 ? 0.2f : 0.8f) *
                                                 80.0f * mp_units::si::metre));
  map.rebuild_geometry_readings ();
  map::Surface& surface = map;

  const float detailed_up = map.normal_at (4, 4)[1];
  const float supported_up =
    surface.snow_support_at (position (Vec3 (40, 0, 40)))
      .numerical_value_in (one);
  // Column 8 borders the low side across the wrap; probe the flat
  // interior instead.
  const float flat_up = surface.snow_support_at (position (Vec3 (60, 0, 40)))
                          .numerical_value_in (one);
  MOPPE_CHECK (supported_up > detailed_up + 0.05f);
  MOPPE_CHECK (flat_up > 0.99f);
}

MOPPE_TEST (home_base_is_a_distinct_materialized_surface_site) {
  using namespace moppe;
  map::Surface map (3, 3, Vec3 (30, 10, 30));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.2f) * 10.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;

  const std::array influence { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                               0.0f, 0.0f, 0.0f, 0.0f };
  const std::array trail {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
  };
  surface.set_use (trail_use_map (surface.domain (), trail, influence));
  const map::SurfaceUseSections* use = surface.atlas ().use ().readings ();
  MOPPE_CHECK (use);
  MOPPE_CHECK (spatial::sample<map::home_base_influence> (
                 *use, position (Vec3 (10, 0, 10))) ==
               1.0f * map::home_base_influence[one]);
  MOPPE_CHECK (spatial::get<map::home_base_influence> (*use)[0] ==
               0.0f * map::home_base_influence[one]);
}

MOPPE_TEST (trail_influence_is_a_materialized_surface_mask) {
  using namespace moppe;
  map::Surface map (3, 3, Vec3 (30, 10, 30));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.2f) * 10.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;

  const std::array influence { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                               0.0f, 0.0f, 0.0f, 0.0f };
  const std::array home {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
  };
  surface.set_use (trail_use_map (surface.domain (), influence, home));
  const map::SurfaceUseSections& use = *surface.atlas ().use ().readings ();
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
  map::Surface map (5, 5, Vec3 (50, 200, 50));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.40f) * 200.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;

  std::vector<float> moisture (25, 0.48f);
  surface.set_moisture (moisture_map (surface.domain (), moisture));
  surface.derive_tree_habitat (50.0f * u::m, 160.0f * u::m);
  const position_t center = position (Vec3 (20, 0, 20));
  MOPPE_CHECK (surface.tree_habitat_at (center) >
               0.8f * map::tree_habitat[one]);

  std::fill (moisture.begin (), moisture.end (), 1.0f);
  surface.set_moisture (moisture_map (surface.domain (), moisture));
  surface.derive_tree_habitat (50.0f * u::m, 160.0f * u::m);
  MOPPE_CHECK (surface.tree_habitat_at (center) <
               0.4f * map::tree_habitat[one]);
}

MOPPE_TEST (forest_cover_is_patchy_deterministic_and_respects_clearings) {
  using namespace moppe;
  map::Surface map (65, 65, Vec3 (320, 180, 320));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.42f) * 180.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;
  const std::vector<float> moisture (65 * 65, 0.48f);
  surface.set_moisture (moisture_map (surface.domain (), moisture));
  surface.derive_tree_habitat (50.0f * u::m, 160.0f * u::m);
  surface.derive_forest_cover (0x12345678U);

  const map::SurfaceForestSections* first_sections =
    surface.atlas ().ecology ().forest_cover ();
  MOPPE_CHECK (first_sections);
  const auto& first = spatial::get<map::forest_cover> (*first_sections);
  std::vector<float> values;
  values.reserve (first.size ());
  for (const map::ForestCover value : first)
    values.push_back (value.numerical_value_in (one));
  MOPPE_CHECK (*std::ranges::max_element (values) > 0.65f);
  MOPPE_CHECK (*std::ranges::min_element (values) < 0.05f);

  surface.derive_forest_cover (0x12345678U);
  const map::SurfaceForestSections* repeated_sections =
    surface.atlas ().ecology ().forest_cover ();
  MOPPE_CHECK (repeated_sections);
  const auto& repeated = spatial::get<map::forest_cover> (*repeated_sections);
  for (std::size_t offset = 0; offset < values.size (); ++offset)
    MOPPE_CHECK_NEAR (
      values[offset], repeated[offset].numerical_value_in (one), 1e-7f);

  const std::vector<float> no_trail (65 * 65, 0.0f);
  const std::vector<float> home_base (65 * 65, 1.0f);
  surface.set_use (trail_use_map (surface.domain (), no_trail, home_base));
  surface.derive_forest_cover (0x12345678U);
  const map::SurfaceForestSections* cleared_sections =
    surface.atlas ().ecology ().forest_cover ();
  MOPPE_CHECK (cleared_sections);
  const auto& cleared = spatial::get<map::forest_cover> (*cleared_sections);
  MOPPE_CHECK (std::ranges::all_of (cleared, [] (map::ForestCover value) {
    return value == 0.0f * map::forest_cover[one];
  }));
}

MOPPE_TEST (surface_reconstruction_matches_authoritative_geometry) {
  using namespace moppe;
  map::Surface map (5, 5, Vec3 (50, 30, 50));
  for (int row = 0; row < map.height (); ++row)
    for (int column = 0; column < map.width (); ++column)
      map.set_elevation (column,
                         row,
                         moppe::terrain::surface_elevation_point (
                           (0.03f * column * column + 0.02f * row) * 30.0f *
                           mp_units::si::metre));
  map.rebuild_geometry_readings ();
  map::Surface& surface = map;

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

MOPPE_TEST (surface_reconstruction_wraps_the_torus) {
  using namespace moppe;
  map::Surface map (5, 5, Vec3 (40, 20, 40));
  for (int row = 0; row < map.height (); ++row)
    for (int column = 0; column < map.width (); ++column)
      map.set_elevation (column,
                         row,
                         moppe::terrain::surface_elevation_point (
                           (0.04f * static_cast<float> (row + 2 * column)) *
                           20.0f * mp_units::si::metre));
  map.rebuild_geometry_readings ();
  map::Surface& surface = map;

  // Sampling one period apart reads the same surface.
  const Vec3 period = map.world_extent ();
  for (const Vec3& point : { Vec3 (3.25f, 0, 7.5f), Vec3 (39.25f, 0, 37.5f) }) {
    const position_t p = position (point);
    const position_t wrapped =
      position (point + Vec3 (period[0], 0.0f, period[2]));
    MOPPE_CHECK_NEAR (elevation_value (surface.elevation_at (p)),
                      elevation_value (surface.elevation_at (wrapped)),
                      1e-5f);
    MOPPE_CHECK_NEAR (elevation_value (surface.elevation_at (p)),
                      map.interpolated_height (point[0], point[2]),
                      1e-5f);
  }
}

MOPPE_TEST (surface_geometry_is_authoritative_without_a_refresh_barrier) {
  using namespace moppe;
  map::Surface map (3, 3, Vec3 (30, 10, 30));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.2f) * 10.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;
  const position_t p = position (Vec3 (5, 0, 5));
  const float before = elevation_value (surface.elevation_at (p));

  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.7f) * 10.0f * mp_units::si::metre));
  MOPPE_CHECK (before != elevation_value (surface.elevation_at (p)));
  MOPPE_CHECK_NEAR (elevation_value (surface.elevation_at (p)), 7.0f, 1e-6f);
}

MOPPE_TEST (surface_presentation_is_the_numeric_bridge_for_typed_sections) {
  using namespace moppe;
  map::Surface map (3, 3, Vec3 (30, 10, 30));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.2f) * 10.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;
  MOPPE_CHECK (!surface.atlas ().hydrology ().channel_flux ());
  MOPPE_CHECK (!surface.atlas ().use ().readings ());

  std::vector<float> trail (9, 0.0f);
  std::vector<float> home (9, 0.0f);
  trail[4] = 0.75f;
  home[4] = 0.25f;
  surface.set_use (trail_use_map (surface.domain (), trail, home));
  MOPPE_CHECK (surface.atlas ().use ().readings ());

  game::SurfacePresentation presentation;
  presentation.refresh (surface);

  MOPPE_CHECK (presentation.trails ().size () == surface.domain ().size ());
  MOPPE_CHECK_NEAR (presentation.trails ()[4], 0.75f, 1e-6f);
  MOPPE_CHECK_NEAR (presentation.home_base ()[4], 0.25f, 1e-6f);
  MOPPE_CHECK (presentation.channel_flux ().empty ());
  MOPPE_CHECK (presentation.snow_support ().size () == 9);
  MOPPE_CHECK (presentation.forest ().empty ());

  surface.rebuild_geometry_readings ();
  MOPPE_CHECK (!surface.atlas ().use ().readings ());
}

MOPPE_TEST (surface_material_sections_keep_meaning_until_the_numeric_bridge) {
  using namespace moppe;
  map::Surface map (3, 3, Vec3 (30, 10, 30));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.2f) * 10.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;

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

  surface.set_moisture (moisture_map (surface.domain (), moisture));
  surface.set_waterline_distance (waterline_map (surface.domain (), distance));
  for (std::size_t cell = 0; cell < eroded.size (); ++cell) {
    const int column = static_cast<int> (cell % 3);
    const int row = static_cast<int> (cell / 3);
    surface.record_material_change (column, row, -eroded[cell]);
    surface.record_material_change (column, row, deposited[cell]);
  }
  surface.derive_geology_materials ();

  static_assert (
    mp_units::QuantityOf<map::SurfaceMoisture, map::surface_moisture>);
  static_assert (
    mp_units::QuantityOf<map::WaterlineDistance, map::waterline_distance>);
  const map::SurfaceMoistureSections* moisture_sections =
    surface.atlas ().hydrology ().moisture ();
  const map::SurfaceWaterlineSections* waterline_sections =
    surface.atlas ().hydrology ().waterline ();
  const map::SurfaceGeologySections* geology_sections =
    surface.atlas ().geology ().materials ();
  MOPPE_CHECK (moisture_sections);
  MOPPE_CHECK (waterline_sections);
  MOPPE_CHECK (geology_sections);
  MOPPE_CHECK (spatial::get<map::surface_moisture> (*moisture_sections)[4] ==
               1.0f * map::surface_moisture[one]);
  MOPPE_CHECK (spatial::get<map::waterline_distance> (*waterline_sections)[4] ==
               2.5f * map::waterline_distance[u::m]);
  const position_t center = position (Vec3 (10, 0, 10));
  MOPPE_CHECK (
    spatial::sample<map::surface_moisture> (*moisture_sections, center) ==
    1.0f * map::surface_moisture[one]);
  MOPPE_CHECK (
    spatial::sample<map::waterline_distance> (*waterline_sections, center) ==
    2.5f * map::waterline_distance[u::m]);
  MOPPE_CHECK_NEAR (spatial::get<map::erosion_exposure> (*geology_sections)[4]
                      .numerical_value_in (one),
                    0.5f,
                    1e-6f);
  MOPPE_CHECK_NEAR (spatial::get<map::deposition_cover> (*geology_sections)[4]
                      .numerical_value_in (one),
                    0.5f,
                    1e-6f);

  game::SurfacePresentation presentation;
  presentation.refresh (surface);
  MOPPE_CHECK_NEAR (presentation.moisture ()[4], 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (presentation.waterline_distance ()[4], 2.5f, 1e-6f);
  MOPPE_CHECK_NEAR (presentation.geology ()[8], 0.5f, 1e-6f);
  MOPPE_CHECK_NEAR (presentation.geology ()[9], 0.5f, 1e-6f);
}
