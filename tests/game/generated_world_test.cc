#include <moppe/game/generated_world.hh>

#include <tests/test.hh>

#include <memory>
#include <type_traits>
#include <vector>

namespace {
  moppe::terrain::WorldRecipe world_without_trails (
    moppe::spatial_extent_t extent, int resolution, moppe::terrain::Seed seed) {
    using namespace moppe;
    using namespace moppe::terrain;
    return make_world_recipe (extent,
                              resolution,
                              seed,
                              50.0f * u::m,
                              TerrainGenerationProfile::Fast)
      .with_terrain_program (
        make_orogeny_program (seed.value, TerrainGenerationProfile::Fast));
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
  const WorldRecipe recipe = world_without_trails (extent, 17, Seed { 42 });

  game::GeneratedWorld world (params, recipe);
  fill_test_terrain (world.surface ());
  world.rebuild_surface ();

  std::vector<game::GeneratedWorld::HydrologyStage> stages;
  world.analyze_hydrology (
    [&stages] (game::GeneratedWorld::HydrologyStage stage) {
      stages.push_back (stage);
    });
  world.derive_surface_readings ();

  MOPPE_CHECK (world.recipe ().seed () == Seed { 42 });
  MOPPE_CHECK (world.params ().water_level == 50.0f * u::m);
  MOPPE_CHECK (world.surface ().domain ().width () == 17);
  MOPPE_CHECK (stages.size () == 6);
  MOPPE_CHECK (world.hydrology ().has_value ());
  MOPPE_CHECK (world.hydrology ()->standing_water ().width () == 17);
  MOPPE_CHECK (world.hydrology ()->lakes ().body.size () == 17 * 17);
  MOPPE_CHECK (world.hydrology ()->drainage ().receiver.size () == 17 * 17);
  MOPPE_CHECK (world.hydrology ()->channels ().domain ().size () == 17 * 17);
  MOPPE_CHECK (world.hydrology ()->waterways ().bodies.size () <=
               world.hydrology ()->lakes ().bodies.size ());
  MOPPE_CHECK (world.water_surface ().has_value ());
  MOPPE_CHECK (world.surface ().atlas ().hydrology ().channel_flux ());
  MOPPE_CHECK (world.surface ().atlas ().hydrology ().moisture ());
  MOPPE_CHECK (world.surface ().atlas ().hydrology ().waterline ());
  MOPPE_CHECK (world.surface ().atlas ().geology ().materials ());
  MOPPE_CHECK (!world.trails ().has_value ());
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
  auto completed = std::make_unique<game::GeneratedWorld> (
    params, world_without_trails (extent, 17, Seed { 72 }));
  const game::GeneratedWorld* address = completed.get ();

  std::unique_ptr<game::GeneratedWorld> active = std::move (completed);

  MOPPE_CHECK (!completed);
  MOPPE_CHECK (active.get () == address);
  MOPPE_CHECK (active->recipe ().seed () == Seed { 72 });
}
