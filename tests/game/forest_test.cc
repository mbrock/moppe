#include <moppe/game/forest.hh>
#include <moppe/map/surface.hh>

#include <tests/recording_renderer.hh>
#include <tests/surface_fixture.hh>
#include <tests/test.hh>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
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
      .form = game::ForestForm::conifer });

  game::save_forest_plan (plan, 99, path.string ());
  const std::optional<game::ForestPlan> restored =
    game::try_load_forest_plan (path.string (), 99, period);
  MOPPE_CHECK (restored.has_value ());
  MOPPE_CHECK (restored->sites.size () == 1);
  MOPPE_CHECK (restored->sites[0].seed == 1234);
  MOPPE_CHECK (restored->sites[0].form == game::ForestForm::conifer);
  MOPPE_CHECK_NEAR (
    position_value (restored->sites[0].position)[1], 34.0f, 0.0f);
  MOPPE_CHECK (!game::try_load_forest_plan (path.string (), 100, period));

  std::ofstream (path, std::ios::binary | std::ios::trunc).put ('x');
  MOPPE_CHECK (!game::try_load_forest_plan (path.string (), 99, period));
  std::filesystem::remove (path);
}

namespace {
  moppe::game::ForestView looking_from (const moppe::Vec3& eye) {
    return { .position = moppe::position (eye) };
  }

  std::size_t average_of (std::span<const std::size_t> counts) {
    std::size_t total = 0;
    for (std::size_t count : counts)
      total += count;
    return counts.empty () ? 0 : total / counts.size ();
  }
}

// Building near geometry on demand is what keeps the world's whole tree
// population from carrying an organism's worth of triangles at once. That
// only holds if a near mesh is both much dearer than the cheap one it stands
// in for, and let go of again once nobody is standing near it.
MOPPE_TEST (near_forest_geometry_follows_the_camera_and_is_released_behind_it) {
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

  // Nothing near has been asked for yet, so only the cheap world-wide meshes
  // are held.
  const std::size_t far_only = forest.resident_bytes ();
  const std::size_t far_meshes = renderer.baked_vertex_counts.size ();
  MOPPE_CHECK (forest.tree_count () > 1000);
  MOPPE_CHECK (far_meshes > 0);
  MOPPE_CHECK (far_only > 0);
  MOPPE_CHECK (forest.resident_chunk_count () == 0);

  // A frame advances a bounded batch rather than every chunk it wants, so
  // arriving somewhere costs coarse trees briefly, not a stall. At most one
  // completed near mesh is uploaded by a call.
  forest.prepare (renderer, looking_from (Vec3 (600, 120, 600)));
  MOPPE_CHECK (forest.resident_chunk_count () <= 1);
  MOPPE_CHECK (renderer.baked_vertex_counts.size () <= far_meshes + 1);
  for (int frame = 0; frame < 32 && forest.resident_chunk_count () == 0;
       ++frame)
    forest.prepare (renderer, looking_from (Vec3 (600, 120, 600)));
  MOPPE_CHECK (forest.resident_chunk_count () == 1);

  const std::span<const std::size_t> baked { renderer.baked_vertex_counts };
  MOPPE_CHECK (average_of (baked.subspan (far_meshes)) >
               2 * average_of (baked.first (far_meshes)));

  const auto settle = [&] (const Vec3& eye) {
    for (int frame = 0; frame < 220; ++frame)
      forest.prepare (renderer, looking_from (eye));
  };

  settle (Vec3 (600, 120, 600));
  const std::size_t chunks_here = forest.resident_chunk_count ();
  const std::size_t near_here = forest.resident_bytes () - far_only;
  MOPPE_CHECK (chunks_here > 2);
  MOPPE_CHECK (near_here > 0);
  // Residency is a neighbourhood, not the world.
  MOPPE_CHECK (chunks_here < 96);

  // The far corner of a periodic world is as far away as anywhere gets.
  settle (Vec3 (1800, 120, 1800));
  const std::size_t near_there = forest.resident_bytes () - far_only;
  MOPPE_CHECK (forest.resident_chunk_count () > 2);
  MOPPE_CHECK (forest.resident_chunk_count () < 96);
  // Had the departed neighbourhood been kept, this would be about double.
  MOPPE_CHECK (near_there < near_here * 3 / 2);
}
