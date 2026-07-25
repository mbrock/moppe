#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <tests/test.hh>

#include <cmath>
MOPPE_TEST (world_recipe_binds_physical_world_to_generation_values) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (5000, 320, 5000)),
                       33,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Fast);

  MOPPE_CHECK (recipe.resolution () == 33);
  MOPPE_CHECK (recipe.seed () == Seed { 77 });
  MOPPE_CHECK (recipe.generation_profile () == TerrainGenerationProfile::Fast);
  MOPPE_CHECK_NEAR (recipe.evolution ().sea_level, 50.0f, 0.0f);
  MOPPE_CHECK_NEAR (recipe.trail_formation ().sea_level, 50.0f, 0.0f);
  MOPPE_CHECK (recipe.evolution ().duration ==
               750000.0f * mp_units::astronomy::Julian_year);
  MOPPE_CHECK (recipe.evolution ().diffusivity ==
               0.0001f * mp_units::si::metre * mp_units::si::metre /
                 mp_units::astronomy::Julian_year);

  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    recipe.resolution (), recipe.resolution (), recipe.extent ()));
  const auto uplift =
    map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
  map::evolve_terrain (surface, uplift, recipe.evolution ());
  MOPPE_CHECK (std::isfinite (surface_elevation_value (
    spatial::get<terrain::surface_elevation> (surface[terrain::TerrainIndex {
      static_cast<std::size_t> (0), static_cast<std::size_t> (0) }]))));
}

MOPPE_TEST (smoke_world_recipe_runs_one_geological_step) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (5000, 320, 5000)),
                       33,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Smoke);

  MOPPE_CHECK (profile_id (recipe.generation_profile ()) == "smoke");
  MOPPE_CHECK (recipe.evolution ().duration == recipe.evolution ().time_step);
}
