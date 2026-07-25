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
    const float duration =
      generation_profile == TerrainGenerationProfile::Smoke      ? 50000.0f
      : generation_profile == TerrainGenerationProfile::Fast     ? 750000.0f
      : generation_profile == TerrainGenerationProfile::Play     ? 1500000.0f
      : generation_profile == TerrainGenerationProfile::Research ? 2000000.0f
                                                                 : 1500000.0f;
    m_evolution.duration = duration * mp_units::astronomy::Julian_year;
    m_evolution.diffusivity = 0.0001f * mp_units::si::metre *
                              mp_units::si::metre /
                              mp_units::astronomy::Julian_year;
    m_evolution.sea_level = meters_value (water_datum);
    m_trail_formation.sea_level = meters_value (water_datum);
  }

  WorldRecipe make_world_recipe (spatial_extent_t extent,
                                 int resolution,
                                 Seed seed,
                                 meters_t water_datum,
                                 TerrainGenerationProfile generation_profile) {
    return { extent, resolution, seed, water_datum, generation_profile };
  }
}
