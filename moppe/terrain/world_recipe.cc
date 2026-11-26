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
    std::optional<SedimentConcentration> sediment_concentration,
    std::optional<proportion_t> critical_hillslope_gradient,
    std::optional<proportion_t> maximum_hillslope_multiplier)
      : m_extent (extent), m_resolution (resolution), m_seed (seed),
        m_water_datum (water_datum), m_generation_profile (generation_profile) {
    // Evolution age and tectonic forcing are separate clocks. The initial
    // orogeny raises the country, then erosion and deposition reorganize that
    // finite relief without banking uplift for the whole geological run.
    constexpr auto year = moppe::astronomy::Julian_year;
    const julian_years_t duration =
      generation_profile == TerrainGenerationProfile::Smoke  ? 50000.0f * year
      : generation_profile == TerrainGenerationProfile::Fast ? 200000.0f * year
      : generation_profile == TerrainGenerationProfile::Play ? 2000000.0f * year
      : generation_profile == TerrainGenerationProfile::Research
        ? 1000000.0f * year
        : 2000000.0f * year;
    m_evolution.duration = duration;
    const julian_years_t profile_uplift_duration =
      generation_profile == TerrainGenerationProfile::Smoke  ? 50000.0f * year
      : generation_profile == TerrainGenerationProfile::Fast ? 50000.0f * year
      : generation_profile == TerrainGenerationProfile::Play ? 500000.0f * year
      : generation_profile == TerrainGenerationProfile::Research
        ? 250000.0f * year
        : 500000.0f * year;
    m_evolution.uplift_duration =
      uplift_duration.value_or (profile_uplift_duration);
    if (channel_initiation_area)
      m_evolution.channel_initiation_area = *channel_initiation_area;
    if (sediment_concentration)
      m_evolution.fluvial_transport.concentration_at_unit_slope =
        *sediment_concentration;
    if (critical_hillslope_gradient)
      m_evolution.critical_hillslope_gradient = *critical_hillslope_gradient;
    if (maximum_hillslope_multiplier)
      m_evolution.maximum_hillslope_diffusivity_multiplier =
        *maximum_hillslope_multiplier;
    const meters_t largest_span =
      std::max (extent_value (extent)[0], extent_value (extent)[2]) * u::m;
    const scale_factor_t span_scale =
      quantity_cast<scale_factor> (largest_span / (10000.0f * u::m));
    const scale_factor_t diffusivity_scale =
      span_scale > 1.0f * scale_factor[one]
        ? std::min (span_scale.numerical_value_in (one) *
                      span_scale.numerical_value_in (one) * 0.85f,
                    24.0f) *
            scale_factor[one]
        : 1.0f * scale_factor[one];
    m_evolution.diffusivity =
      0.0001f * diffusivity_scale.numerical_value_in (one) *
      mp_units::si::metre * mp_units::si::metre / moppe::astronomy::Julian_year;
    if (!channel_initiation_area && span_scale > 1.0f * scale_factor[one]) {
      // Modest hillslope headwater threshold on macro worlds (> 10 km):
      // keeps alpine ridges sharp while avoiding dense single-cell rilling.
      const scale_factor_t extra =
        (span_scale.numerical_value_in (one) - 1.0f) * scale_factor[one];
      m_evolution.channel_initiation_area =
        8000.0f * extra.numerical_value_in (one) * u::m * u::m;
    }
    m_evolution.sea_level = surface_elevation_point (water_datum);
    m_evolution.lithology = make_lithology (seed,
                                            extent_value (extent)[0] * u::m,
                                            extent_value (extent)[2] * u::m,
                                            m_evolution.sea_level);
    m_trail_formation.sea_level = surface_elevation_point (water_datum);
  }

  WorldRecipe make_world_recipe (
    spatial_extent_t extent,
    int resolution,
    Seed seed,
    meters_t water_datum,
    TerrainGenerationProfile generation_profile,
    std::optional<julian_years_t> uplift_duration,
    std::optional<square_meters_t> channel_initiation_area,
    std::optional<SedimentConcentration> sediment_concentration,
    std::optional<proportion_t> critical_hillslope_gradient,
    std::optional<proportion_t> maximum_hillslope_multiplier) {
    return { extent,
             resolution,
             seed,
             water_datum,
             generation_profile,
             uplift_duration,
             channel_initiation_area,
             sediment_concentration,
             critical_hillslope_gradient,
             maximum_hillslope_multiplier };
  }
}
