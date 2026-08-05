#include <moppe/terrain/stream_power_evolution.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  constexpr std::array profile_heights { 0.0f, 10.0f, 20.0f, 30.0f, 40.0f,
                                         0.0f, 10.0f, 20.0f, 30.0f, 40.0f };

  TerrainDomain profile_grid () {
    return { 5, 2, 10.0f * u::m, 10.0f * u::m };
  }

  std::vector<meters_per_julian_year_t> uniform_uplift (std::size_t count,
                                                        float meters_per_year) {
    return std::vector<meters_per_julian_year_t> (
      count,
      meters_per_year * mp_units::si::metre / mp_units::astronomy::Julian_year);
  }
}

MOPPE_TEST (zero_duration_stream_power_evolution_is_identity) {
  const ElevationMap terrain =
    make_elevation_map (profile_grid (), profile_heights);
  const auto uplift = uniform_uplift (profile_heights.size (), 0.001f);
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain, uplift, { .duration = 0.0f * mp_units::astronomy::Julian_year });

  MOPPE_CHECK (result.heights == spatial::get<surface_elevation> (terrain));
  MOPPE_CHECK (result.channel_tangents.size () == profile_heights.size ());
  for (const ChannelTangent tangent : result.channel_tangents)
    MOPPE_CHECK (tangent.magnitude () == 0.0f * mp_units::one);
  MOPPE_CHECK (result.report.steps == iteration_count (0));
}

MOPPE_TEST (zero_incision_applies_spatial_uplift_and_fixes_the_ocean) {
  const ElevationMap terrain =
    make_elevation_map (profile_grid (), profile_heights);
  auto uplift = uniform_uplift (profile_heights.size (), 0.0f);
  uplift[4] = 0.002f * mp_units::si::metre / mp_units::astronomy::Julian_year;
  uplift[9] = uplift[4];
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain,
    uplift,
    { .duration = 1000.0f * mp_units::astronomy::Julian_year,
      .time_step = 250.0f * mp_units::astronomy::Julian_year,
      .reference_incision_rate =
        0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year,
      .sea_level = 0.0f });

  MOPPE_CHECK_NEAR (surface_elevation_value (result.heights[0]), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (surface_elevation_value (result.heights[4]), 42.0f, 1e-6f);
  MOPPE_CHECK_NEAR (
    surface_elevation_value (result.heights[3]), profile_heights[3], 1e-7f);
  MOPPE_CHECK (result.report.steps == iteration_count (4));
  MOPPE_CHECK (result.report.fixed_boundaries == cell_count (2));
}

MOPPE_TEST (fixed_ocean_preserves_its_submerged_bathymetry) {
  constexpr std::array heights { -20.0f, -10.0f, 20.0f, 30.0f };
  const ElevationMap terrain = make_elevation_map (
    TerrainDomain (
      2, 2, 10.0f * mp_units::si::metre, 10.0f * mp_units::si::metre),
    heights);
  const auto uplift = uniform_uplift (heights.size (), 0.001f);
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain,
    uplift,
    { .duration = 1000.0f * mp_units::astronomy::Julian_year,
      .time_step = 1000.0f * mp_units::astronomy::Julian_year,
      .reference_incision_rate =
        0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year,
      .sea_level = 0.0f });

  MOPPE_CHECK_NEAR (
    surface_elevation_value (result.heights[0]), heights[0], 0.0f);
  MOPPE_CHECK_NEAR (
    surface_elevation_value (result.heights[1]), heights[1], 0.0f);
  MOPPE_CHECK (result.heights[2] > surface_elevation_point (heights[2] * u::m));
  MOPPE_CHECK (result.heights[3] > surface_elevation_point (heights[3] * u::m));
}

MOPPE_TEST (one_implicit_step_matches_the_closed_form_solution) {
  constexpr std::array heights { 0.0f, 100.0f, 0.0f, 100.0f };
  const ElevationMap terrain = make_elevation_map (
    TerrainDomain (
      2, 2, 10.0f * mp_units::si::metre, 10.0f * mp_units::si::metre),
    heights);
  const auto uplift = uniform_uplift (heights.size (), 0.0f);
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain,
    uplift,
    { .duration = 100.0f * mp_units::astronomy::Julian_year,
      .time_step = 100.0f * mp_units::astronomy::Julian_year,
      .reference_incision_rate =
        0.1f * mp_units::si::metre / mp_units::astronomy::Julian_year,
      .area_exponent = 0.0f,
      // A four-cell world is entirely below any real channel head, so say
      // that this case is about the fluvial law and not about where the
      // channel network begins.
      .channel_initiation_area =
        1.0f * mp_units::si::metre * mp_units::si::metre,
      .sea_level = 0.0f });

  // dt v_ref / distance = 1, so z' = (100 m + 1 * 0 m) / (1 + 1).
  MOPPE_CHECK_NEAR (surface_elevation_value (result.heights[0]), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (surface_elevation_value (result.heights[1]), 50.0f, 1e-6f);
  MOPPE_CHECK_NEAR (surface_elevation_value (result.heights[2]), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (surface_elevation_value (result.heights[3]), 50.0f, 1e-6f);
}

MOPPE_TEST (one_square_meter_reference_preserves_legacy_calibration) {
  constexpr std::array heights { 0.0f, 100.0f, 0.0f, 100.0f };
  const ElevationMap terrain = make_elevation_map (
    TerrainDomain (
      2, 2, 10.0f * mp_units::si::metre, 10.0f * mp_units::si::metre),
    heights);
  const auto uplift = uniform_uplift (heights.size (), 0.0f);
  constexpr float duration_years = 1000.0f;
  constexpr float legacy_k = 2e-5f;
  constexpr float area_m2 = 100.0f;
  constexpr float exponent = 0.4f;
  constexpr float distance_m = 10.0f;
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain,
    uplift,
    { .duration = duration_years * mp_units::astronomy::Julian_year,
      .time_step = duration_years * mp_units::astronomy::Julian_year,
      .reference_incision_rate =
        legacy_k * mp_units::si::metre / mp_units::astronomy::Julian_year,
      .area_exponent = exponent,
      .channel_initiation_area =
        1.0f * mp_units::si::metre * mp_units::si::metre,
      .sea_level = 0.0f });
  const float legacy_weight =
    duration_years * legacy_k * std::pow (area_m2, exponent) / distance_m;
  const float expected = 100.0f / (1.0f + legacy_weight);

  MOPPE_CHECK_NEAR (
    surface_elevation_value (result.heights[1]), expected, 1e-7f);
  MOPPE_CHECK_NEAR (
    surface_elevation_value (result.heights[3]), expected, 1e-7f);
}

MOPPE_TEST (reference_area_reparameterization_preserves_incision) {
  const ElevationMap terrain =
    make_elevation_map (profile_grid (), profile_heights);
  const auto uplift = uniform_uplift (profile_heights.size (), 0.0f);
  constexpr float exponent = 0.4f;
  const auto evolve = [&] (float reference_area_m2,
                           float reference_rate_m_per_year) {
    return evolve_stream_power (
      terrain,
      uplift,
      { .duration = 10000.0f * mp_units::astronomy::Julian_year,
        .time_step = 1000.0f * mp_units::astronomy::Julian_year,
        .reference_incision_rate = reference_rate_m_per_year *
                                   mp_units::si::metre /
                                   mp_units::astronomy::Julian_year,
        .reference_area =
          reference_area_m2 * mp_units::si::metre * mp_units::si::metre,
        .area_exponent = exponent,
        .sea_level = 0.0f });
  };
  const StreamPowerEvolutionResult one_square_meter = evolve (1.0f, 2e-5f);
  const StreamPowerEvolutionResult one_hundred_square_meters =
    evolve (100.0f, 2e-5f * std::pow (100.0f, exponent));

  for (std::size_t cell = 0; cell < profile_heights.size (); ++cell)
    MOPPE_CHECK_NEAR (
      surface_elevation_value (one_square_meter.heights[cell]),
      surface_elevation_value (one_hundred_square_meters.heights[cell]),
      1e-6f);
}

MOPPE_TEST (depression_routing_closes_the_sediment_ledger) {
  constexpr std::array basin { 0.0f, 30.0f, 10.0f, 40.0f, 50.0f,
                               0.0f, 30.0f, 10.0f, 40.0f, 50.0f };
  const ElevationMap terrain = make_elevation_map (profile_grid (), basin);
  const auto uplift = uniform_uplift (basin.size (), 0.0f);
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain,
    uplift,
    { .duration = 100000.0f * mp_units::astronomy::Julian_year,
      .time_step = 10000.0f * mp_units::astronomy::Julian_year,
      .sea_level = 0.0f });

  const auto cubic_metre = u::m * u::m * u::m;
  const double detached =
    result.report.incised_volume.numerical_value_in (cubic_metre);
  const double deposited =
    result.report.deposited_volume.numerical_value_in (cubic_metre);
  const double exported =
    result.report.exported_sediment_volume.numerical_value_in (cubic_metre);
  MOPPE_CHECK (detached > 0.0);
  MOPPE_CHECK (deposited > 0.0);
  MOPPE_CHECK_NEAR (static_cast<float> (detached),
                    static_cast<float> (deposited + exported),
                    static_cast<float> (detached * 1e-6));
  MOPPE_CHECK_NEAR (
    static_cast<float> (
      result.report.sediment_balance_residual.numerical_value_in (cubic_metre)),
    0.0f,
    1e-5f);
  for (const SedimentThickness thickness : result.sediment_thickness)
    MOPPE_CHECK (thickness >= SedimentThickness::zero ());
}

MOPPE_TEST (stream_power_evolution_is_bit_deterministic) {
  const ElevationMap terrain =
    make_elevation_map (profile_grid (), profile_heights);
  const auto uplift = uniform_uplift (profile_heights.size (), 0.0005f);
  const StreamPowerEvolution parameters {
    .duration = 50000.0f * mp_units::astronomy::Julian_year,
    .time_step = 5000.0f * mp_units::astronomy::Julian_year,
    .sea_level = 0.0f
  };
  const StreamPowerEvolutionResult first =
    evolve_stream_power (terrain, uplift, parameters);
  const StreamPowerEvolutionResult second =
    evolve_stream_power (terrain, uplift, parameters);

  MOPPE_CHECK (first.heights == second.heights);
  MOPPE_CHECK (first.channel_tangents == second.channel_tangents);
  MOPPE_CHECK (std::any_of (first.channel_tangents.begin (),
                            first.channel_tangents.end (),
                            [] (ChannelTangent tangent) {
                              return tangent.magnitude () >
                                     0.0f * mp_units::one;
                            }));
}

MOPPE_TEST (stream_power_interleaves_stable_hillslope_diffusion) {
  std::array<float, 25> heights;
  heights.fill (0.2f);
  heights[12] = 1.0f;
  const ElevationMap terrain = make_elevation_map (
    TerrainDomain (
      5, 5, 1.0f * mp_units::si::metre, 1.0f * mp_units::si::metre),
    heights);
  const auto uplift = uniform_uplift (heights.size (), 0.0f);
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain,
    uplift,
    { .duration = 1.0f * mp_units::astronomy::Julian_year,
      .time_step = 1.0f * mp_units::astronomy::Julian_year,
      .reference_incision_rate =
        0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year,
      .diffusivity = 0.1f * mp_units::si::metre * mp_units::si::metre /
                     mp_units::astronomy::Julian_year,
      .sea_level = -1.0f });

  MOPPE_CHECK (result.heights[12] <
               surface_elevation_point (heights[12] * u::m));
  MOPPE_CHECK (result.heights[11] >
               surface_elevation_point (heights[11] * u::m));
  MOPPE_CHECK (result.report.diffusion_sweeps == iteration_count (1));
}

MOPPE_TEST (hillslope_catchments_are_left_to_creep) {
  // The same two ridges twice: once with a channel head far below their
  // catchment, once with one far above it. Running water cuts the first and
  // leaves the second for the hillslope pass, which is the whole content of
  // the channel-initiation area.
  constexpr std::array heights { 0.0f, 100.0f, 0.0f, 100.0f };
  const ElevationMap terrain = make_elevation_map (
    TerrainDomain (
      2, 2, 10.0f * mp_units::si::metre, 10.0f * mp_units::si::metre),
    heights);
  const auto uplift = uniform_uplift (heights.size (), 0.0f);
  const auto evolve = [&] (float initiation_m2) {
    return evolve_stream_power (
      terrain,
      uplift,
      { .duration = 100.0f * mp_units::astronomy::Julian_year,
        .time_step = 100.0f * mp_units::astronomy::Julian_year,
        .reference_incision_rate =
          0.1f * mp_units::si::metre / mp_units::astronomy::Julian_year,
        .area_exponent = 0.0f,
        .channel_initiation_area =
          initiation_m2 * mp_units::si::metre * mp_units::si::metre,
        .sea_level = 0.0f });
  };

  MOPPE_CHECK_NEAR (
    surface_elevation_value (evolve (1.0f).heights[1]), 50.0f, 1e-6f);
  MOPPE_CHECK_NEAR (
    surface_elevation_value (evolve (100000.0f).heights[1]), 100.0f, 1e-6f);
}
