#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/world_recipe.hh>

#include <tests/test.hh>

#include <cmath>
#include <variant>

MOPPE_TEST (world_recipe_binds_physical_world_to_its_program) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (640, 650, 640)),
                       33,
                       Topology::Torus,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Fast);

  MOPPE_CHECK (recipe.resolution () == 33);
  MOPPE_CHECK (recipe.topology () == Topology::Torus);
  MOPPE_CHECK (recipe.seed () == Seed { 77 });
  MOPPE_CHECK (recipe.generation_profile () == TerrainGenerationProfile::Fast);
  MOPPE_CHECK_NEAR (recipe.normalized_water_datum (), 50.0f / 650.0f, 0.0f);

  const TerrainProgram& program = recipe.terrain_program ();
  MOPPE_CHECK (program.seed == recipe.seed ());
  MOPPE_CHECK_NEAR (
    program.source.sea_level, recipe.normalized_water_datum (), 0.0f);
  const auto& orogeny =
    std::get<OrogenyEvolution> (program.transforms.front ());
  const auto& trails = std::get<TrailFormation> (program.transforms.back ());
  MOPPE_CHECK_NEAR (
    orogeny.evolution.sea_level, recipe.normalized_water_datum (), 0.0f);
  MOPPE_CHECK_NEAR (trails.sea_level, recipe.normalized_water_datum (), 0.0f);

  TerrainProgram edited_program = program;
  edited_program.source.sea_level = 0.25f;
  const WorldRecipe edited = recipe.with_terrain_program (edited_program);
  MOPPE_CHECK_NEAR (recipe.terrain_program ().source.sea_level,
                    recipe.normalized_water_datum (),
                    0.0f);
  MOPPE_CHECK_NEAR (edited.terrain_program ().source.sea_level, 0.25f, 0.0f);

  map::RandomHeightMap map (recipe.resolution (),
                            recipe.resolution (),
                            extent_value (recipe.extent ()),
                            recipe.seed ().value,
                            recipe.topology ());
  map::TerrainEvaluator (map).evaluate (recipe.terrain_program ());
  MOPPE_CHECK (std::isfinite (map.get (0, 0)));
  MOPPE_CHECK (map.get (0, 0) == map.get (map.width () - 1, 0));
  MOPPE_CHECK (map.get (0, 0) == map.get (0, map.height () - 1));
}
