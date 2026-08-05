#include <moppe/terrain/world_recipe.hh>

namespace moppe::terrain {
  std::string_view profile_id (TerrainGenerationProfile profile) noexcept {
    switch (profile) {
    case TerrainGenerationProfile::Smoke:
      return "smoke";
    case TerrainGenerationProfile::Fast:
      return "fast";
    case TerrainGenerationProfile::Play:
      return "play";
    case TerrainGenerationProfile::Research:
      return "research";
    }
    return "play";
  }

  WorldRecipe::WorldRecipe (spatial_extent_t extent,
                            int resolution,
                            Seed seed,
                            meters_t water_datum,
                            TerrainGenerationProfile generation_profile)
      : m_extent (extent), m_resolution (resolution), m_seed (seed),
        m_water_datum (water_datum), m_generation_profile (generation_profile) {
    // How long the world erodes also decides how tall it ends up, because this
    // landscape does not approach steady state over these runs: relief grows
    // nearly linearly with the clock. Play nevertheless uses the deliberately
    // long two-million-year evolution selected from current visual evaluation.
    // See docs/what-age-does-to-this-world.md for the earlier shorter-world
    // evidence and the subsequent decision reversal.
    const float duration =
      generation_profile == TerrainGenerationProfile::Smoke      ? 50000.0f
      : generation_profile == TerrainGenerationProfile::Fast     ? 200000.0f
      : generation_profile == TerrainGenerationProfile::Play     ? 2000000.0f
      : generation_profile == TerrainGenerationProfile::Research ? 1000000.0f
                                                                 : 2000000.0f;
    m_evolution.duration = duration * mp_units::astronomy::Julian_year;
    m_evolution.diffusivity = 0.0001f * mp_units::si::metre *
                              mp_units::si::metre /
                              mp_units::astronomy::Julian_year;
    m_evolution.sea_level = (water_datum).numerical_value_in (moppe::u::m);
    m_trail_formation.sea_level =
      (water_datum).numerical_value_in (moppe::u::m);
  }

  WorldRecipe make_world_recipe (spatial_extent_t extent,
                                 int resolution,
                                 Seed seed,
                                 meters_t water_datum,
                                 TerrainGenerationProfile generation_profile) {
    return { extent, resolution, seed, water_datum, generation_profile };
  }
}
