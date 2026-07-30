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
    // How long the world erodes decides how tall it ends up, because this
    // landscape never approaches the steady state where incision balances
    // uplift: relief grows very nearly linearly with the clock. So these
    // numbers are the world's height, and the height they are chosen for is
    // the vertical extent the world declares -- `WorldParams::map_size` is
    // 320 m, and Play at half a million years fills it. Tripling them once
    // produced 755 m of relief in a five-kilometre world, which is a canyon
    // badlands rather than a landscape. See
    // docs/what-age-does-to-this-world.md.
    const float duration =
      generation_profile == TerrainGenerationProfile::Smoke      ? 50000.0f
      : generation_profile == TerrainGenerationProfile::Fast     ? 200000.0f
      : generation_profile == TerrainGenerationProfile::Play     ? 500000.0f
      : generation_profile == TerrainGenerationProfile::Research ? 1000000.0f
                                                                 : 500000.0f;
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
