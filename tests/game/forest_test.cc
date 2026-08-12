#include <moppe/game/forest.hh>
#include <moppe/map/surface.hh>

#include <tests/recording_renderer.hh>
#include <tests/surface_fixture.hh>
#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

static_assert (std::same_as<decltype (moppe::game::ForestSite {}.position),
                            moppe::position_t>);
static_assert (std::same_as<decltype (moppe::game::ForestSite {}.normal),
                            moppe::terrain::TerrainNormal>);
static_assert (std::same_as<decltype (moppe::game::ForestSite {}.cover),
                            moppe::map::ForestCover>);
static_assert (std::same_as<decltype (moppe::game::ForestSite {}.moisture),
                            moppe::map::SurfaceMoisture>);
static_assert (std::same_as<decltype (moppe::game::ForestSite {}.size),
                            moppe::game::TreeSizeFactor>);
static_assert (std::same_as<decltype (moppe::game::ForestPlan {}.period),
                            moppe::spatial_extent_t>);
static_assert (std::same_as<decltype (moppe::render::ForestInstance {}.height),
                            moppe::meters_t>);
static_assert (
  std::same_as<decltype (moppe::render::ForestInstance {}.crown_radius),
               moppe::meters_t>);

MOPPE_TEST (global_forest_sites_are_stable_and_follow_canopy_cover) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    129, 129, spatial_extent_in_metres (Vec3 (640, 0, 640))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.42f) * 180.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  const map::SurfaceReadings readings = test::complete_readings (
    surface,
    { .moisture = test::uniform_moisture (surface.domain (), 0.48f),
      .seed = 0xdecafbadU });

  const game::ForestPlan first =
    game::plan_global_forest (surface, readings, 0xa511e9b3U);
  const game::ForestPlan second =
    game::plan_global_forest (surface, readings, 0xa511e9b3U);
  MOPPE_CHECK (first.sites.size () > 100);
  MOPPE_CHECK (first.sites.size () == second.sites.size ());
  for (std::size_t index = 0; index < first.sites.size (); ++index) {
    const game::ForestSite& site = first.sites[index];
    MOPPE_CHECK (site.seed == second.sites[index].seed);
    MOPPE_CHECK_NEAR (position_value (site.position)[0],
                      position_value (second.sites[index].position)[0],
                      1e-6f);
    MOPPE_CHECK_NEAR (position_value (site.position)[2],
                      position_value (second.sites[index].position)[2],
                      1e-6f);
    MOPPE_CHECK (site.cover >= 0.06f * map::forest_cover[mp_units::one]);
    MOPPE_CHECK (site.normal.numerical_value_in (mp_units::one)[1] > 0.99f);
  }
}

MOPPE_TEST (global_forest_sites_leave_materialized_clearings_empty) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    65, 65, spatial_extent_in_metres (Vec3 (320, 0, 320))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.42f) * 180.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  const map::SurfaceReadings readings = test::complete_readings (
    surface,
    { .moisture = test::uniform_moisture (surface.domain (), 0.48f),
      .use = test::uniform_use (surface.domain (), 0.0f, 1.0f),
      .seed = 0xfeed1234U });

  MOPPE_CHECK (
    game::plan_global_forest (surface, readings, 0x31415926U).sites.empty ());
}

MOPPE_TEST (global_forest_population_has_a_periodic_hard_core) {
  using namespace moppe;
  constexpr float period = 160.0f;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    65, 65, spatial_extent_in_metres (Vec3 (period, 0, period))));
  std::ranges::fill (
    spatial::get<terrain::surface_elevation> (surface),
    terrain::surface_elevation_point (0.42f * 180.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  map::SurfaceReadings readings = test::complete_readings (surface);
  std::ranges::fill (spatial::get<map::forest_cover> (readings),
                     1.0f * map::forest_cover[mp_units::one]);

  const game::ForestPlan plan =
    game::plan_global_forest (surface, readings, 0x96c41d2bU);
  MOPPE_CHECK (plan.sites.size () > 700);
  for (std::size_t i = 0; i < plan.sites.size (); ++i)
    for (std::size_t j = i + 1; j < plan.sites.size (); ++j) {
      const Vec3 a = position_value (plan.sites[i].position);
      const Vec3 b = position_value (plan.sites[j].position);
      const float dx = std::abs (a[0] - b[0]);
      const float dz = std::abs (a[2] - b[2]);
      const float periodic_x = std::min (dx, period - dx);
      const float periodic_z = std::min (dz, period - dz);
      MOPPE_CHECK (periodic_x * periodic_x + periodic_z * periodic_z >=
                   4.0f - 1e-4f);
    }
}

MOPPE_TEST (baked_forest_plan_round_trips_and_rejects_bad_identity) {
  using namespace moppe;
  const std::filesystem::path path =
    std::filesystem::temp_directory_path () / "moppe-forest-plan-test.bin";
  const spatial_extent_t period = spatial_extent_in_metres (Vec3 (640, 0, 480));
  game::ForestPlan plan;
  plan.period = period;
  plan.sites.push_back (
    { .position = position (Vec3 (12, 34, 56)),
      .normal = Vec3 (0, 1, 0) * terrain::terrain_normal[mp_units::one],
      .cover = 0.7f * map::forest_cover[mp_units::one],
      .moisture = 0.4f * map::surface_moisture[mp_units::one],
      .size = 1.2f * game::tree_size_factor[mp_units::one],
      .seed = 1234,
      .form = game::ForestForm::conifer,
      .age = game::ForestAge::ancient });

  game::save_forest_plan (plan, 99, path.string ());
  const std::optional<game::ForestPlan> restored =
    game::try_load_forest_plan (path.string (), 99, period);
  MOPPE_CHECK (restored.has_value ());
  MOPPE_CHECK (restored->sites.size () == 1);
  MOPPE_CHECK (restored->sites[0].seed == 1234);
  MOPPE_CHECK (restored->sites[0].form == game::ForestForm::conifer);
  MOPPE_CHECK (restored->sites[0].age == game::ForestAge::ancient);
  MOPPE_CHECK_NEAR (
    position_value (restored->sites[0].position)[1], 34.0f, 0.0f);
  MOPPE_CHECK (!game::try_load_forest_plan (path.string (), 100, period));

  std::ofstream (path, std::ios::binary | std::ios::trunc).put ('x');
  MOPPE_CHECK (!game::try_load_forest_plan (path.string (), 99, period));
  std::filesystem::remove (path);
}

// The production seam carries semantic individuals once. It must not bake or
// retain complete tree meshes: projected detail belongs to the object/mesh
// stages, where reusable organs can be expanded only when visible.
MOPPE_TEST (forest_uploads_typed_individuals_without_baking_tree_meshes) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    129, 129, spatial_extent_in_metres (Vec3 (2400, 0, 2400))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       0.42f * 180.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  const map::SurfaceReadings readings = test::complete_readings (
    surface,
    { .moisture = test::uniform_moisture (surface.domain (), 0.48f),
      .seed = 0xdecafbadU });

  test::RecordingRenderer renderer;
  game::ForestLandscape forest;
  forest.rebuild (renderer, surface, readings, 0xa511e9b3U);

  MOPPE_CHECK (forest.tree_count () > 1000);
  MOPPE_CHECK (renderer.forest_instances.size () == forest.tree_count ());
  MOPPE_CHECK (renderer.baked_vertex_counts.empty ());
  MOPPE_CHECK (forest.resident_bytes () ==
               forest.tree_count () * sizeof (render::ForestInstance));
  MOPPE_CHECK (extent_value (renderer.forest_setup.period)[0] == 2400.0f);

  std::array<bool, 4> ages {};
  for (const render::ForestInstance& tree : renderer.forest_instances) {
    MOPPE_CHECK (tree.height > 0.0f * u::m);
    MOPPE_CHECK (tree.crown_radius > 0.0f * u::m);
    MOPPE_CHECK (tree.canopy_cover >= 0.0f * proportion[mp_units::one]);
    ages[static_cast<std::size_t> (tree.age)] = true;
  }
  MOPPE_CHECK (std::ranges::count (ages, true) >= 3);

  forest.draw (renderer);
  MOPPE_CHECK (renderer.forest_draws == 1);
}
