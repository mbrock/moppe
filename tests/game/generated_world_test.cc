#include <moppe/game/generated_world.hh>

#include <tests/test.hh>

#include <vector>

MOPPE_TEST (generated_world_owns_a_complete_named_world) {
  using namespace moppe;
  using namespace moppe::terrain;

  const spatial_extent_t extent =
    spatial_extent_in_metres (Vec3 (640, 650, 640));
  game::WorldParams params;
  params.map_size = extent;
  params.resolution = 17;
  params.water_level = 50.0f * u::m;
  params.terrain_topology = Topology::Torus;
  const WorldRecipe recipe =
    make_geological_world_recipe (extent,
                                  17,
                                  Topology::Torus,
                                  Seed { 42 },
                                  50.0f * u::m,
                                  TerrainGenerationProfile::Fast);

  game::GeneratedWorld world (params, recipe);
  game::GeneratedWorld::Builder build = world.build ();
  build.terrain ().randomize_uniformly ();
  build.rebuild_surface ();

  std::vector<game::GeneratedWorld::HydrologyStage> stages;
  build.analyze_hydrology (
    [&stages] (game::GeneratedWorld::HydrologyStage stage) {
      stages.push_back (stage);
    });
  build.materialize_analyses ();

  MOPPE_CHECK (world.recipe ().seed () == Seed { 42 });
  MOPPE_CHECK (world.params ().water_level == 50.0f * u::m);
  MOPPE_CHECK (world.surface ().atlas ().geometry ().domain ().width () == 17);
  MOPPE_CHECK (stages.size () == 6);
  MOPPE_CHECK (world.hydrology ().has_value ());
  MOPPE_CHECK (world.hydrology ()->standing_water ().width () == 16);
  MOPPE_CHECK (world.hydrology ()->lakes ().body.size () == 16 * 16);
  MOPPE_CHECK (world.hydrology ()->drainage ().receiver.size () == 16 * 16);
  MOPPE_CHECK (world.hydrology ()->channels ().domain ().size () == 16 * 16);
  MOPPE_CHECK (world.hydrology ()->waterways ().bodies.size () <=
               world.hydrology ()->lakes ().bodies.size ());
  MOPPE_CHECK (world.water_surface ().has_value ());
  MOPPE_CHECK (world.surface ().atlas ().hydrology ().channel_flux ());
  MOPPE_CHECK (world.surface ().atlas ().hydrology ().moisture ());
  MOPPE_CHECK (world.surface ().atlas ().hydrology ().waterline ());
  MOPPE_CHECK (world.surface ().atlas ().geology ().materials ());
  MOPPE_CHECK (!world.trails ().has_value ());

  const auto* stable_terrain = &world.terrain ();
  const auto* stable_surface = &world.surface ();
  build.reset (make_geological_world_recipe (extent,
                                             17,
                                             Topology::Torus,
                                             Seed { 43 },
                                             50.0f * u::m,
                                             TerrainGenerationProfile::Fast));
  MOPPE_CHECK (&world.terrain () == stable_terrain);
  MOPPE_CHECK (&world.surface () == stable_surface);
  MOPPE_CHECK (world.recipe ().seed () == Seed { 43 });
  MOPPE_CHECK (world.terrain_history ().empty ());
  MOPPE_CHECK (!world.hydrology ().has_value ());
  MOPPE_CHECK (!world.water_surface ().has_value ());
  MOPPE_CHECK (!world.trails ().has_value ());
}
