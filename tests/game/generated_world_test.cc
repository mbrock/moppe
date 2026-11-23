#include <moppe/game/generated_world.hh>
#include <moppe/game/world_cache.hh>

#include <tests/test.hh>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <type_traits>
#include <vector>

using namespace moppe;

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

    terrain::TrailNetwork trails {
      .domain = surface.domain (),
      .use = terrain::TrailUseMap (surface.domain ()),
    };
    trails.earthwork_delta.resize (surface.domain ().size (),
                                   0.0f * terrain::surface_elevation[u::m]);
    auto [roads, use] = game::form_world_roads (surface, recipe, trails);
    game::Hydrology hydrology =
      game::analyze_hydrology (surface, recipe, progress);
    auto [water, readings] =
      game::analyze_surface (surface, recipe, hydrology, use);
    game::ForestPlan forest = game::plan_global_forest (
      surface, readings, water, recipe.seed ().value ^ 0xa34c91e5U);
    return std::make_unique<game::GeneratedWorld> (params,
                                                   recipe,
                                                   std::move (surface),
                                                   std::move (hydrology),
                                                   std::move (water),
                                                   std::move (trails),
                                                   std::move (readings),
                                                   std::move (roads),
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
  const auto identity = game::read_world_cache_identity (cache.string ());
  MOPPE_CHECK (identity && identity->extent == recipe.extent ());
  std::ofstream (cache / "old-marker") << "old";
  game::save_world_cache (*written, cache.string ());
  MOPPE_CHECK (!std::filesystem::exists (cache / "old-marker"));
  std::filesystem::remove (cache / "topology.bin");
  MOPPE_CHECK (!game::try_load_world_cache (
    game::WorldParams {}, recipe, cache.string ()));
  game::save_world_cache (*written, cache.string ());
  MOPPE_CHECK (game::try_load_world_cache (
                 game::WorldParams {}, recipe, cache.string ()) != nullptr);

  std::exception_ptr failure[2];
  auto save = [&] (int index) {
    try {
      game::save_world_cache (*written, cache.string ());
    } catch (...) {
      failure[index] = std::current_exception ();
    }
  };
  std::thread first (save, 0);
  std::thread second (save, 1);
  first.join ();
  second.join ();
  MOPPE_CHECK (!failure[0] && !failure[1]);

  const std::unique_ptr<game::GeneratedWorld> restored =
    game::try_load_world_cache (game::WorldParams {}, recipe, cache.string ());
  MOPPE_CHECK (restored != nullptr);
  MOPPE_CHECK (restored->surface ().size () == written->surface ().size ());
  MOPPE_CHECK (restored->readings ().size () == written->readings ().size ());
  MOPPE_CHECK (restored->water_surface ().size () ==
               written->water_surface ().size ());
  MOPPE_CHECK (restored->roads () == written->roads ());
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
                       750000.0f * moppe::astronomy::Julian_year);
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
  const WorldRecipe other_hillslope =
    make_world_recipe (extent,
                       17,
                       Seed { 91 },
                       50.0f * u::m,
                       TerrainGenerationProfile::Fast,
                       std::nullopt,
                       std::nullopt,
                       std::nullopt,
                       0.8f * proportion[mp_units::one],
                       4.0f * proportion[mp_units::one]);
  MOPPE_CHECK (!game::try_load_world_cache (
    game::WorldParams {}, other_hillslope, cache.string ()));
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
                       750000.0f * moppe::astronomy::Julian_year);
  const WorldRecipe other_channel_scale =
    make_world_recipe (extent,
                       17,
                       Seed { 91 },
                       50.0f * u::m,
                       TerrainGenerationProfile::Fast,
                       std::nullopt,
                       1200.0f * u::m * u::m);
  const WorldRecipe other_hillslope =
    make_world_recipe (extent,
                       17,
                       Seed { 91 },
                       50.0f * u::m,
                       TerrainGenerationProfile::Fast,
                       std::nullopt,
                       std::nullopt,
                       std::nullopt,
                       0.8f * proportion[mp_units::one],
                       4.0f * proportion[mp_units::one]);
  const game::WorldCacheConfig automatic;
  const std::string automatic_path = game::world_cache_name (first, automatic);
  MOPPE_CHECK (automatic_path.find ("world-default-") != std::string::npos);
  MOPPE_CHECK (automatic_path != game::world_cache_name (second, automatic));
  MOPPE_CHECK (automatic_path !=
               game::world_cache_name (other_uplift, automatic));
  MOPPE_CHECK (automatic_path !=
               game::world_cache_name (other_channel_scale, automatic));
  MOPPE_CHECK (automatic_path !=
               game::world_cache_name (other_hillslope, automatic));

  game::WorldCacheConfig named { .key = "terrain-tuning" };
  const std::string first_path = game::world_cache_name (first, named);
  const std::string second_path = game::world_cache_name (second, named);
  MOPPE_CHECK (first_path.find ("world-key-terrain-tuning-") !=
               std::string::npos);
  MOPPE_CHECK (first_path != second_path);

  named.mode = game::WorldCacheMode::Disabled;
  MOPPE_CHECK (game::world_cache_name (first, named).empty ());
}

MOPPE_TEST (runtime_ground_matches_dense_queries_across_the_world_seam) {
  using namespace moppe::terrain;
  const auto extent = spatial_extent_in_metres (Vec3 (640, 650, 640));
  auto world = build_test_world (test_world_recipe (extent, 17, Seed { 8 }),
                                 game::WorldParams {});
  const std::array sites { position (Vec3 (31.1f, 0, 91.f)),
                           position (Vec3 (-4.5f, 0, 638.f)),
                           position (Vec3 (1287.8f, 0, -3.f)) };
  std::vector<map::SurfaceNormal> normals;
  std::vector<map::TrailInfluence> trail;
  std::vector<map::ForestCover> pasture;
  for (const auto& at : sites) {
    normals.push_back (spatial::sample<terrain_normal> (world->surface (), at));
    trail.push_back (world->reading<map::trail_influence> (at));
    pasture.push_back (world->reading<map::forest_cover> (at));
  }
  const auto* heights =
    spatial::get<surface_elevation> (world->surface ()).data ();
  const auto water_bodies = world->lake_census ().domain ().size ();
  world->enter_play ();
  world->enter_play ();
  MOPPE_CHECK (spatial::get<surface_elevation> (world->surface ()).data () ==
               heights);
  MOPPE_CHECK (spatial::get<terrain_normal> (world->surface ()).empty ());
  MOPPE_CHECK (world->water_bodies ().size () == water_bodies);
  for (size_t i = 0; i < sites.size (); ++i) {
    const auto normal = map::ground_normal_at (world->surface (), sites[i]);
    MOPPE_CHECK (length ((normal - normals[i]).numerical_value_in (one)) <
                 0.0001f);
    MOPPE_CHECK (
      std::abs ((world->reading<map::trail_influence> (sites[i]) - trail[i])
                  .numerical_value_in (one)) <= 0.5f / 255.f + 1e-6f);
    MOPPE_CHECK (
      std::abs ((world->reading<map::forest_cover> (sites[i]) - pasture[i])
                  .numerical_value_in (one)) <= 0.5f / 255.f + 1e-6f);
  }
}

MOPPE_TEST (runtime_water_preserves_banks_waves_and_vertical_current) {
  using namespace moppe::terrain;
  const TerrainDomain domain (13, 7);
  std::vector<SurfaceElevation> bed (domain.size (),
                                     surface_elevation_point (20.f * u::m));
  WaterSheets sheets (domain);
  auto& levels = spatial::get<surface_elevation> (sheets);
  levels = bed;
  for (size_t i : { 0u, 63u, 64u, 90u }) {
    levels[i] = surface_elevation_point ((i % 2 ? 19.f : 24.f) * u::m);
    spatial::get<wave_amplitude> (sheets)[i] = 0.7f * wave_amplitude[one];
    spatial::get<water_velocity> (sheets)[i] =
      Vec3 (0.4f, -1.2f, 3.f) * water_velocity[u::m / u::s];
  }
  const RuntimeWater compact (sheets, bed);
  for (int z = -1; z <= 7; ++z)
    for (int x = -1; x <= 13; ++x) {
      const auto at = position (Vec3 (x + 0.375f, 0, z + 0.75f));
      MOPPE_CHECK (compact.sample<surface_elevation> (at) ==
                   spatial::sample<surface_elevation> (sheets, at));
      MOPPE_CHECK (compact.sample<wave_amplitude> (at) ==
                   spatial::sample<wave_amplitude> (sheets, at));
      const auto difference = compact.sample<water_velocity> (at) -
                              spatial::sample<water_velocity> (sheets, at);
      MOPPE_CHECK (length2 (difference.numerical_value_in (u::m / u::s)) == 0);
    }
}

MOPPE_TEST (packed_cover_reconstructs_all_bit_widths_and_edge_tiles) {
  const terrain::TerrainDomain d (75, 11);
  std::vector<map::SurfaceMoisture> moisture (d.size ());
  for (size_t z = 0; z < d.height (); ++z)
    for (size_t x = 0; x < d.width (); ++x) {
      const unsigned bits = (x / 8) % 9;
      const unsigned mask = (1u << bits) - 1;
      const unsigned offset = (255 - mask) / 2;
      const unsigned value =
        offset + ((x % 8 == 7 ? mask : 23 * x + 31 * z) & mask);
      moisture[z * d.width () + x] = value / 255.f * map::surface_moisture[one];
    }
  game::CoverGrid grid;
  grid.assign (d, moisture);
  for (size_t z = 0; z < d.height (); ++z)
    for (size_t x = 0; x < d.width (); ++x)
      MOPPE_CHECK (
        grid.at ({ x, z }) ==
        std::lround (255.f *
                     moisture[z * d.width () + x].numerical_value_in (one)));
  std::ranges::fill (moisture, .4f * map::surface_moisture[one]);
  grid.assign (d, moisture);
  MOPPE_CHECK (grid.storage_bytes () < d.size () / 4);
  MOPPE_CHECK (grid.at ({ 74, 10 }) == 102);
}
