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

  WorldRecipe::WorldRecipe (
    spatial_extent_t extent,
    int resolution,
    Seed seed,
    meters_t water_datum,
    TerrainGenerationProfile generation_profile,
    std::optional<julian_years_t> uplift_duration,
    std::optional<square_meters_t> channel_initiation_area,
    std::optional<SedimentConcentration> sediment_concentration)
      : m_extent (extent), m_resolution (resolution), m_seed (seed),
        m_water_datum (water_datum), m_generation_profile (generation_profile) {
    // Evolution age and tectonic forcing are separate clocks. The initial
    // orogeny raises the country, then erosion and deposition reorganize that
    // finite relief without banking uplift for the whole geological run.
    const float duration =
      generation_profile == TerrainGenerationProfile::Smoke      ? 50000.0f
      : generation_profile == TerrainGenerationProfile::Fast     ? 200000.0f
      : generation_profile == TerrainGenerationProfile::Play     ? 2000000.0f
      : generation_profile == TerrainGenerationProfile::Research ? 1000000.0f
                                                                 : 2000000.0f;
    m_evolution.duration = duration * mp_units::astronomy::Julian_year;
    const float profile_uplift_duration =
      generation_profile == TerrainGenerationProfile::Smoke      ? 50000.0f
      : generation_profile == TerrainGenerationProfile::Fast     ? 50000.0f
      : generation_profile == TerrainGenerationProfile::Play     ? 500000.0f
      : generation_profile == TerrainGenerationProfile::Research ? 250000.0f
                                                                 : 500000.0f;
    m_evolution.uplift_duration = uplift_duration.value_or (
      profile_uplift_duration * mp_units::astronomy::Julian_year);
    if (channel_initiation_area)
      m_evolution.channel_initiation_area = *channel_initiation_area;
    if (sediment_concentration)
      m_evolution.fluvial_transport.concentration_at_unit_slope =
        *sediment_concentration;
    m_evolution.diffusivity = 0.0001f * mp_units::si::metre *
                              mp_units::si::metre /
                              mp_units::astronomy::Julian_year;
    m_evolution.sea_level = (water_datum).numerical_value_in (moppe::u::m);
    m_trail_formation.sea_level =
      (water_datum).numerical_value_in (moppe::u::m);
  }

  WorldRecipe make_world_recipe (
    spatial_extent_t extent,
    int resolution,
    Seed seed,
    meters_t water_datum,
    TerrainGenerationProfile generation_profile,
    std::optional<julian_years_t> uplift_duration,
    std::optional<square_meters_t> channel_initiation_area,
    std::optional<SedimentConcentration> sediment_concentration) {
    return { extent,
             resolution,
             seed,
             water_datum,
             generation_profile,
             uplift_duration,
             channel_initiation_area,
             sediment_concentration };
  }
}
