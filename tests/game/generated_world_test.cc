#include <moppe/game/generated_world.hh>
#include <moppe/game/landscape_summary.hh>
#include <moppe/game/world_cache.hh>

#include <tests/test.hh>

#include <filesystem>
#include <memory>
#include <sstream>
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

MOPPE_TEST (landscape_summary_measures_one_complete_world) {
  using namespace moppe;
  using namespace moppe::terrain;

  const spatial_extent_t extent =
    spatial_extent_in_metres (Vec3 (640, 650, 640));
  game::WorldParams params;
  params.map_size = extent;
  params.resolution = 17;
  params.water_level = 50.0f * u::m;
  const WorldRecipe recipe =
    make_world_recipe (extent,
                       17,
                       Seed { 42 },
                       50.0f * u::m,
                       TerrainGenerationProfile::Fast,
                       750000.0f * mp_units::astronomy::Julian_year);
  const std::unique_ptr<game::GeneratedWorld> world =
    build_test_world (recipe, params);
  const auto& [flood, census, drainage, rivers] = world->hydrology ();
  const game::LandscapeSummary summary = game::summarize_landscape (
    world->surface (), flood, census, drainage, rivers, world->recipe ());

  MOPPE_CHECK (summary.seed == 42u);
  MOPPE_CHECK (summary.resolution == 17);
  MOPPE_CHECK_NEAR (static_cast<float> (summary.uplift_years), 750000.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    static_cast<float> (summary.channel_initiation_area_m2), 1.0f, 0.0f);
  MOPPE_CHECK_NEAR (static_cast<float> (summary.runoff_m_per_year), 1.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    static_cast<float> (summary.sediment_concentration_at_unit_slope),
    2e-5f,
    0.0f);
  MOPPE_CHECK (summary.land_cells > 0);
  MOPPE_CHECK (summary.land_area_m2 > 0.0);
  MOPPE_CHECK (summary.land_elevation_max_m >= summary.land_elevation_p90_m);
  MOPPE_CHECK (summary.land_below_10_deg_fraction >=
               summary.land_below_5_deg_fraction);
  MOPPE_CHECK_NEAR (
    static_cast<float> (summary.eroded_sediment_m3), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    static_cast<float> (summary.deposited_sediment_m3), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    static_cast<float> (summary.mobile_sediment_m3), 0.0f, 0.0f);

  std::ostringstream output;
  game::write_landscape_summary_csv (output, summary);
  MOPPE_CHECK (output.str ().starts_with (
    "seed,resolution,spacing_x_m,spacing_z_m,evolution_years,"));
  MOPPE_CHECK (output.str ().find (",200000,750000,1,1,") != std::string::npos);
  MOPPE_CHECK (output.str ().find ("sediment_concentration_at_unit_slope") !=
               std::string::npos);

  std::ostringstream elevation_output (std::ios::out | std::ios::binary);
  game::write_landscape_elevation_f32 (elevation_output, world->surface ());
  MOPPE_CHECK (elevation_output.str ().size () ==
               world->surface ().size () * sizeof (float));
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
  const WorldRecipe other_uplift =
    make_world_recipe (extent,
                       17,
                       Seed { 91 },
                       50.0f * u::m,
                       TerrainGenerationProfile::Fast,
                       750000.0f * mp_units::astronomy::Julian_year);
  MOPPE_CHECK (!game::try_load_world_cache (
    game::WorldParams {}, other_uplift, cache.string ()));
  const WorldRecipe other_channel_scale =
    make_world_recipe (extent,
                       17,
                       Seed { 91 },
                       50.0f * u::m,
                       TerrainGenerationProfile::Fast,
                       std::nullopt,
                       1200.0f * u::m * u::m);
  MOPPE_CHECK (!game::try_load_world_cache (
    game::WorldParams {}, other_channel_scale, cache.string ()));
  std::filesystem::remove_all (cache);
}

MOPPE_TEST (world_cache_names_are_stable_and_recipe_specific) {
  using namespace moppe;
  using namespace moppe::terrain;

  const spatial_extent_t extent =
    spatial_extent_in_metres (Vec3 (640, 650, 640));
  const WorldRecipe first = test_world_recipe (extent, 17, Seed { 91 });
  const WorldRecipe second = test_world_recipe (extent, 17, Seed { 92 });
  const WorldRecipe other_uplift =
    make_world_recipe (extent,
                       17,
                       Seed { 91 },
                       50.0f * u::m,
                       TerrainGenerationProfile::Fast,
                       750000.0f * mp_units::astronomy::Julian_year);
  const WorldRecipe other_channel_scale =
    make_world_recipe (extent,
                       17,
                       Seed { 91 },
                       50.0f * u::m,
                       TerrainGenerationProfile::Fast,
                       std::nullopt,
                       1200.0f * u::m * u::m);
  const game::WorldCacheConfig automatic;
  const std::string automatic_path = game::world_cache_name (first, automatic);
  MOPPE_CHECK (automatic_path.find ("world-default-") != std::string::npos);
  MOPPE_CHECK (automatic_path != game::world_cache_name (second, automatic));
  MOPPE_CHECK (automatic_path !=
               game::world_cache_name (other_uplift, automatic));
  MOPPE_CHECK (automatic_path !=
               game::world_cache_name (other_channel_scale, automatic));

  game::WorldCacheConfig named { .key = "terrain-tuning" };
  const std::string first_path = game::world_cache_name (first, named);
  const std::string second_path = game::world_cache_name (second, named);
  MOPPE_CHECK (first_path.find ("world-key-terrain-tuning-") !=
               std::string::npos);
  MOPPE_CHECK (first_path != second_path);

  named.mode = game::WorldCacheMode::Disabled;
  MOPPE_CHECK (game::world_cache_name (first, named).empty ());
}
