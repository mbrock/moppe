#include <moppe/game/generated_world.hh>
#include <moppe/game/world_cache.hh>

#include <tests/test.hh>

#include <filesystem>
#include <memory>
#include <type_traits>
#include <vector>

namespace {
  moppe::terrain::WorldRecipe test_world_recipe (moppe::spatial_extent_t extent,
                                                 int resolution,
                                                 moppe::terrain::Seed seed) {
    using namespace moppe;
    using namespace moppe::terrain;
    return make_world_recipe (
      extent, resolution, seed, 50.0f * u::m, TerrainGenerationProfile::Fast);
  }

  void fill_test_terrain (moppe::map::SurfaceGeometry& surface) {
    for (int y = 0; y < static_cast<int> (surface.domain ().height ()); ++y)
      for (int x = 0; x < static_cast<int> (surface.domain ().width ()); ++x)
        spatial::get<terrain::surface_elevation> (
          surface[terrain::TerrainIndex { static_cast<std::size_t> (x),
                                          static_cast<std::size_t> (y) }]) =
          moppe::terrain::surface_elevation_point (
            (0.25f + 0.01f * static_cast<float> ((x + y) % 7)) * 650.0f *
            mp_units::si::metre);
  }

  // The build order the loading worker runs: evolve a surface, analyze its
  // water, then assemble the world from finished parts.
  std::unique_ptr<moppe::game::GeneratedWorld>
  build_test_world (const moppe::terrain::WorldRecipe& recipe,
                    const moppe::game::WorldParams& params,
                    const moppe::game::HydrologyProgress& progress = {}) {
    using namespace moppe;
    map::SurfaceGeometry surface =
      map::SurfaceGeometry (terrain::TerrainDomain (
        recipe.resolution (), recipe.resolution (), recipe.extent ()));
    fill_test_terrain (surface);
    map::rebuild_geometry (surface);

    game::HydrologyAnalysis analysis =
      game::analyze_hydrology (surface, recipe, progress);
    terrain::TrailNetwork trails {
      .domain = surface.domain (),
      .use = terrain::TrailUseMap (surface.domain ()),
    };
    trails.earthwork_delta_m.resize (surface.domain ().size (), 0.0f);
    auto [water, readings] = game::analyze_surface (
      surface, recipe, analysis.hydrology, analysis.channels, trails.use);
    game::ForestPlan forest = game::plan_global_forest (
      surface, readings, recipe.seed ().value ^ 0xa34c91e5U);
    return std::make_unique<game::GeneratedWorld> (
      params,
      recipe,
      std::move (surface),
      std::move (analysis.hydrology),
      std::move (water),
      std::move (trails),
      std::move (readings),
      std::move (forest));
  }
}

MOPPE_TEST (generated_world_owns_a_complete_named_world) {
  using namespace moppe;
  using namespace moppe::terrain;

  const spatial_extent_t extent =
    spatial_extent_in_metres (Vec3 (640, 650, 640));
  game::WorldParams params;
  params.map_size = extent;
  params.resolution = 17;
  params.water_level = 50.0f * u::m;
  const WorldRecipe recipe = test_world_recipe (extent, 17, Seed { 42 });

  std::vector<game::HydrologyStage> stages;
  const std::unique_ptr<game::GeneratedWorld> world =
    build_test_world (recipe, params, [&stages] (game::HydrologyStage stage) {
      stages.push_back (stage);
    });

  MOPPE_CHECK (world->recipe ().seed () == Seed { 42 });
  MOPPE_CHECK (world->params ().water_level == 50.0f * u::m);
  MOPPE_CHECK (world->surface ().domain ().width () == 17);
  MOPPE_CHECK (stages.size () == 5);
  const auto& [standing_water, lakes, drainage, rivers] = world->hydrology ();
  MOPPE_CHECK (standing_water.width () == 17);
  MOPPE_CHECK (lakes.cell_count () == 17 * 17);
  MOPPE_CHECK (drainage.receiver.size () == 17 * 17);
  MOPPE_CHECK (rivers.body_traversed.size () == lakes.domain ().size ());
  MOPPE_CHECK (world->water_surface ().size () == 17 * 17);
  MOPPE_CHECK (world->readings ().size () == 17 * 17);
  MOPPE_CHECK (world->trails ().domain == world->surface ().domain ());
  MOPPE_CHECK (world->forest ().period ==
               spatial_extent_in_metres (Vec3 (640, 0, 640)));
}

MOPPE_TEST (generated_world_handoffs_move_the_owner_not_the_world) {
  using namespace moppe;
  using namespace moppe::terrain;

  static_assert (!std::is_move_constructible_v<game::GeneratedWorld>);
  static_assert (!std::is_move_assignable_v<game::GeneratedWorld>);

  const spatial_extent_t extent =
    spatial_extent_in_metres (Vec3 (640, 650, 640));
  game::WorldParams params;
  params.map_size = extent;
  params.resolution = 17;
  params.water_level = 50.0f * u::m;
  auto completed =
    build_test_world (test_world_recipe (extent, 17, Seed { 72 }), params);
  const game::GeneratedWorld* address = completed.get ();

  std::unique_ptr<game::GeneratedWorld> active = std::move (completed);

  MOPPE_CHECK (!completed);
  MOPPE_CHECK (active.get () == address);
  MOPPE_CHECK (active->recipe ().seed () == Seed { 72 });
}

MOPPE_TEST (finished_world_cache_round_trips_every_owned_artifact) {
  using namespace moppe;
  using namespace moppe::terrain;

  const std::filesystem::path cache =
    std::filesystem::temp_directory_path () / "moppe-world-cache-test.world";
  std::filesystem::remove_all (cache);
  const spatial_extent_t extent =
    spatial_extent_in_metres (Vec3 (640, 650, 640));
  const WorldRecipe recipe = test_world_recipe (extent, 17, Seed { 91 });
  const std::unique_ptr<game::GeneratedWorld> written =
    build_test_world (recipe, game::WorldParams {});
  game::save_world_cache (*written, cache.string ());

  const std::unique_ptr<game::GeneratedWorld> restored =
    game::try_load_world_cache (game::WorldParams {}, recipe, cache.string ());
  MOPPE_CHECK (restored != nullptr);
  MOPPE_CHECK (restored->surface ().size () == written->surface ().size ());
  MOPPE_CHECK (restored->readings ().size () == written->readings ().size ());
  MOPPE_CHECK (restored->water_surface ().size () ==
               written->water_surface ().size ());
  MOPPE_CHECK (restored->forest ().period == written->forest ().period);
  MOPPE_CHECK (restored->forest ().sites.size () ==
               written->forest ().sites.size ());
  MOPPE_CHECK (std::filesystem::is_regular_file (cache / "forest-plan.bin"));

  const WorldRecipe other_seed = test_world_recipe (extent, 17, Seed { 92 });
  MOPPE_CHECK (!game::try_load_world_cache (
    game::WorldParams {}, other_seed, cache.string ()));
  std::filesystem::remove_all (cache);
}

MOPPE_TEST (named_world_cache_key_replaces_only_the_build_identity) {
  using namespace moppe;
  using namespace moppe::terrain;

  const spatial_extent_t extent =
    spatial_extent_in_metres (Vec3 (640, 650, 640));
  const WorldRecipe first = test_world_recipe (extent, 17, Seed { 91 });
  const WorldRecipe second = test_world_recipe (extent, 17, Seed { 92 });
  const game::WorldCacheConfig automatic;
  MOPPE_CHECK (game::world_cache_name (first, automatic, "build-a") !=
               game::world_cache_name (first, automatic, "build-b"));

  game::WorldCacheConfig named { .key = "terrain-tuning" };
  const std::string first_path =
    game::world_cache_name (first, named, "build-a");
  const std::string second_path =
    game::world_cache_name (second, named, "build-b");
  MOPPE_CHECK (first_path.find ("world-key-terrain-tuning-") !=
               std::string::npos);
  MOPPE_CHECK (first_path ==
               game::world_cache_name (first, named, "another-build"));
  MOPPE_CHECK (first_path != second_path);

  named.mode = game::WorldCacheMode::Disabled;
  MOPPE_CHECK (game::world_cache_name (first, named, "build-a").empty ());
}
