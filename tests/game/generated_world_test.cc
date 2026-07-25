#include <moppe/game/generated_world.hh>

#include <tests/test.hh>

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

  void fill_test_terrain (moppe::map::Surface& map) {
    for (int y = 0; y < map.height (); ++y)
      for (int x = 0; x < map.width (); ++x)
        map.set_elevation (
          x,
          y,
          moppe::terrain::surface_elevation_point (
            (0.25f + 0.01f * static_cast<float> ((x + y) % 7)) * 650.0f *
            mp_units::si::metre));
  }

  // The build order the loading worker runs: evolve a surface, analyze its
  // water, then assemble the world from finished parts.
  std::unique_ptr<moppe::game::GeneratedWorld>
  build_test_world (const moppe::terrain::WorldRecipe& recipe,
                    const moppe::game::WorldParams& params,
                    const moppe::game::HydrologyProgress& progress = {}) {
    using namespace moppe;
    map::Surface surface (recipe.resolution (),
                          recipe.resolution (),
                          extent_value (recipe.extent ()));
    fill_test_terrain (surface);
    surface.rebuild_geometry ();

    game::Hydrology hydrology =
      game::analyze_hydrology (surface.geometry (), recipe, progress);
    terrain::TrailNetwork trails {
      .domain = surface.domain (),
      .use = terrain::TrailUseMap (surface.domain ()),
    };
    auto [water, readings] = game::analyze_surface (
      surface.geometry (), recipe, hydrology, trails.use);
    return std::make_unique<game::GeneratedWorld> (params,
                                                   recipe,
                                                   std::move (surface),
                                                   std::move (hydrology),
                                                   std::move (water),
                                                   std::move (trails),
                                                   std::move (readings));
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
  const auto& [standing_water, lakes, drainage, channels, rivers] =
    world->hydrology ();
  MOPPE_CHECK (standing_water.width () == 17);
  MOPPE_CHECK (lakes.body.size () == 17 * 17);
  MOPPE_CHECK (drainage.receiver.size () == 17 * 17);
  MOPPE_CHECK (channels.domain ().size () == 17 * 17);
  MOPPE_CHECK (world->water_surface ().size () == 17 * 17);
  MOPPE_CHECK (world->readings ().size () == 17 * 17);
  MOPPE_CHECK (world->trails ().domain == world->surface ().domain ());
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
