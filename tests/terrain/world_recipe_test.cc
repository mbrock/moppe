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
               200000.0f * mp_units::astronomy::Julian_year);
  MOPPE_CHECK (recipe.evolution ().uplift_duration ==
               50000.0f * mp_units::astronomy::Julian_year);
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
  MOPPE_CHECK (recipe.evolution ().uplift_duration ==
               recipe.evolution ().duration);
}

MOPPE_TEST (play_world_recipe_relaxes_after_half_a_million_years_of_uplift) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (5000, 320, 5000)),
                       33,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Play);

  MOPPE_CHECK (recipe.evolution ().duration ==
               2000000.0f * mp_units::astronomy::Julian_year);
  MOPPE_CHECK (recipe.evolution ().uplift_duration ==
               500000.0f * mp_units::astronomy::Julian_year);
}

MOPPE_TEST (research_world_recipe_has_a_finite_uplift_schedule) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (5000, 320, 5000)),
                       33,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Research);

  MOPPE_CHECK (recipe.evolution ().duration ==
               1000000.0f * mp_units::astronomy::Julian_year);
  MOPPE_CHECK (recipe.evolution ().uplift_duration ==
               250000.0f * mp_units::astronomy::Julian_year);
}

MOPPE_TEST (world_recipe_accepts_an_explicit_uplift_experiment) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (5000, 320, 5000)),
                       33,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Play,
                       750000.0f * mp_units::astronomy::Julian_year);

  MOPPE_CHECK (recipe.evolution ().duration ==
               2000000.0f * mp_units::astronomy::Julian_year);
  MOPPE_CHECK (recipe.evolution ().uplift_duration ==
               750000.0f * mp_units::astronomy::Julian_year);
}

MOPPE_TEST (world_recipe_accepts_a_physical_channel_head_experiment) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (5000, 320, 5000)),
                       33,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Play,
                       std::nullopt,
                       1200.0f * mp_units::si::metre * mp_units::si::metre);

  MOPPE_CHECK (recipe.evolution ().channel_initiation_area ==
               1200.0f * mp_units::si::metre * mp_units::si::metre);
}

MOPPE_TEST (world_recipe_accepts_a_sediment_capacity_experiment) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (5000, 320, 5000)),
                       33,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Play,
                       std::nullopt,
                       std::nullopt,
                       0.00001f * sediment_concentration[mp_units::one]);

  MOPPE_CHECK (
    recipe.evolution ().fluvial_transport.concentration_at_unit_slope ==
    0.00001f * sediment_concentration[mp_units::one]);
}

MOPPE_TEST (world_recipe_accepts_a_critical_hillslope_experiment) {
  using namespace moppe;
  using namespace moppe::terrain;

  const WorldRecipe recipe =
    make_world_recipe (spatial_extent_in_metres (Vec3 (5000, 320, 5000)),
                       33,
                       Seed { 77 },
                       50.0f * mp_units::si::metre,
                       TerrainGenerationProfile::Play,
                       std::nullopt,
                       std::nullopt,
                       std::nullopt,
                       0.8f * proportion[mp_units::one],
                       4.0f * proportion[mp_units::one]);

  MOPPE_CHECK (recipe.evolution ().critical_hillslope_gradient ==
               0.8f * proportion[mp_units::one]);
  MOPPE_CHECK (recipe.evolution ().maximum_hillslope_diffusivity_multiplier ==
               4.0f * proportion[mp_units::one]);
}
