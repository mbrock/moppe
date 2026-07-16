#include <moppe/terrain/analytical_erosion.hh>
#include <moppe/terrain/stream_power_evolution.hh>

#include <tests/test.hh>

#include <array>
#include <cmath>
#include <vector>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  constexpr std::array profile_heights { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f,
                                         0.0f, 0.1f, 0.2f, 0.3f, 0.4f };

  TerrainGrid profile_grid () {
    return { .width = 5,
             .height = 2,
             .spacing_x = 10.0f * mp_units::si::metre,
             .spacing_y = 10.0f * mp_units::si::metre,
             .height_scale = 100.0f * mp_units::si::metre };
  }

  std::vector<meters_per_julian_year_t> uniform_uplift (std::size_t count,
                                                        float meters_per_year) {
    return std::vector<meters_per_julian_year_t> (
      count,
      meters_per_year * mp_units::si::metre / mp_units::astronomy::Julian_year);
  }
}

MOPPE_TEST (zero_duration_stream_power_evolution_is_identity) {
  const TerrainView terrain (profile_grid (), profile_heights);
  const auto uplift = uniform_uplift (profile_heights.size (), 0.001f);
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain, uplift, { .duration = 0.0f * mp_units::astronomy::Julian_year });

  MOPPE_CHECK (result.heights == std::vector<float> (profile_heights.begin (),
                                                     profile_heights.end ()));
  MOPPE_CHECK (result.report.steps == iteration_count (0));
}

MOPPE_TEST (zero_incision_applies_spatial_uplift_and_fixes_the_ocean) {
  const TerrainView terrain (profile_grid (), profile_heights);
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

  MOPPE_CHECK_NEAR (result.heights[0], 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (result.heights[4], 0.42f, 1e-6f);
  MOPPE_CHECK_NEAR (result.heights[3], profile_heights[3], 1e-7f);
  MOPPE_CHECK (result.report.steps == iteration_count (4));
  MOPPE_CHECK (result.report.fixed_boundaries == cell_count (2));
}

MOPPE_TEST (fixed_ocean_preserves_its_submerged_bathymetry) {
  constexpr std::array heights { -0.2f, -0.1f, 0.2f, 0.3f };
  const TerrainView terrain ({ .width = 2,
                               .height = 2,
                               .spacing_x = 10.0f * mp_units::si::metre,
                               .spacing_y = 10.0f * mp_units::si::metre,
                               .height_scale = 100.0f * mp_units::si::metre },
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

  MOPPE_CHECK_NEAR (result.heights[0], heights[0], 0.0f);
  MOPPE_CHECK_NEAR (result.heights[1], heights[1], 0.0f);
  MOPPE_CHECK (result.heights[2] > heights[2]);
  MOPPE_CHECK (result.heights[3] > heights[3]);
}

MOPPE_TEST (one_implicit_step_matches_the_closed_form_solution) {
  constexpr std::array heights { 0.0f, 1.0f, 0.0f, 1.0f };
  const TerrainView terrain ({ .width = 2,
                               .height = 2,
                               .spacing_x = 10.0f * mp_units::si::metre,
                               .spacing_y = 10.0f * mp_units::si::metre,
                               .height_scale = 100.0f * mp_units::si::metre },
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
      .sea_level = 0.0f,
      .routing = StreamPowerRouting::D8 });

  // dt v_ref / distance = 1, so z' = (100 m + 1 * 0 m) / (1 + 1).
  MOPPE_CHECK_NEAR (result.heights[0], 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (result.heights[1], 0.5f, 1e-7f);
  MOPPE_CHECK_NEAR (result.heights[2], 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (result.heights[3], 0.5f, 1e-7f);
}

MOPPE_TEST (one_square_meter_reference_preserves_legacy_calibration) {
  constexpr std::array heights { 0.0f, 1.0f, 0.0f, 1.0f };
  const TerrainView terrain ({ .width = 2,
                               .height = 2,
                               .spacing_x = 10.0f * mp_units::si::metre,
                               .spacing_y = 10.0f * mp_units::si::metre,
                               .height_scale = 100.0f * mp_units::si::metre },
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
      .sea_level = 0.0f,
      .routing = StreamPowerRouting::D8 });
  const float legacy_weight =
    duration_years * legacy_k * std::pow (area_m2, exponent) / distance_m;
  const float expected = 1.0f / (1.0f + legacy_weight);

  MOPPE_CHECK_NEAR (result.heights[1], expected, 1e-7f);
  MOPPE_CHECK_NEAR (result.heights[3], expected, 1e-7f);
}

MOPPE_TEST (reference_area_reparameterization_preserves_incision) {
  const TerrainView terrain (profile_grid (), profile_heights);
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
        .sea_level = 0.0f,
        .routing = StreamPowerRouting::D8 });
  };
  const StreamPowerEvolutionResult one_square_meter = evolve (1.0f, 2e-5f);
  const StreamPowerEvolutionResult one_hundred_square_meters =
    evolve (100.0f, 2e-5f * std::pow (100.0f, exponent));

  for (std::size_t cell = 0; cell < profile_heights.size (); ++cell)
    MOPPE_CHECK_NEAR (one_square_meter.heights[cell],
                      one_hundred_square_meters.heights[cell],
                      1e-6f);
}

MOPPE_TEST (implicit_stream_power_converges_toward_the_analytical_profile) {
  const TerrainView terrain (profile_grid (), profile_heights);
  const auto uplift = uniform_uplift (profile_heights.size (), 0.0f);
  constexpr float duration = 10000.0f;
  constexpr float erodibility = 2e-5f;
  constexpr float area_exponent = 0.4f;
  const AnalyticalErosionResult analytical = erode_analytically (
    terrain,
    { .duration = duration * mp_units::astronomy::Julian_year,
      .erodibility = erodibility,
      .area_exponent = area_exponent,
      .sea_level = 0.0f });
  const auto evolve = [&] (float dt) {
    return evolve_stream_power (
      terrain,
      uplift,
      { .duration = duration * mp_units::astronomy::Julian_year,
        .time_step = dt * mp_units::astronomy::Julian_year,
        .reference_incision_rate =
          erodibility * mp_units::si::metre / mp_units::astronomy::Julian_year,
        .area_exponent = area_exponent,
        .sea_level = 0.0f,
        .routing = StreamPowerRouting::D8 });
  };
  const StreamPowerEvolutionResult coarse = evolve (5000.0f);
  const StreamPowerEvolutionResult fine = evolve (100.0f);
  const auto error = [&] (const StreamPowerEvolutionResult& result) {
    double sum = 0.0;
    for (std::size_t i = 0; i < result.heights.size (); ++i)
      sum += std::fabs (result.heights[i] - analytical.heights[i]);
    return sum;
  };

  MOPPE_CHECK (error (fine) < error (coarse));
  MOPPE_CHECK (fine.report.lowered_volume > 0.0);
  MOPPE_CHECK (fine.report.raised_volume == 0.0);
  MOPPE_CHECK (fine.report.incised_volume > 0.0);
}

MOPPE_TEST (depression_routing_never_turns_incision_into_deposition) {
  constexpr std::array basin { 0.0f, 0.3f, 0.1f, 0.4f, 0.5f,
                               0.0f, 0.3f, 0.1f, 0.4f, 0.5f };
  const TerrainView terrain (profile_grid (), basin);
  const auto uplift = uniform_uplift (basin.size (), 0.0f);
  const StreamPowerEvolutionResult result = evolve_stream_power (
    terrain,
    uplift,
    { .duration = 100000.0f * mp_units::astronomy::Julian_year,
      .time_step = 10000.0f * mp_units::astronomy::Julian_year,
      .sea_level = 0.0f });

  for (std::size_t cell = 0; cell < basin.size (); ++cell)
    MOPPE_CHECK (result.heights[cell] <= basin[cell] + 1e-7f);
}

MOPPE_TEST (stream_power_evolution_is_bit_deterministic) {
  const TerrainView terrain (profile_grid (), profile_heights);
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
}

MOPPE_TEST (stream_power_interleaves_stable_hillslope_diffusion) {
  std::array<float, 25> heights;
  heights.fill (0.2f);
  heights[12] = 1.0f;
  const TerrainView terrain ({ .width = 5,
                               .height = 5,
                               .spacing_x = 1.0f * mp_units::si::metre,
                               .spacing_y = 1.0f * mp_units::si::metre,
                               .height_scale = 1.0f * mp_units::si::metre },
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

  MOPPE_CHECK (result.heights[12] < heights[12]);
  MOPPE_CHECK (result.heights[11] > heights[11]);
  MOPPE_CHECK (result.report.diffusion_sweeps == iteration_count (1));
}
