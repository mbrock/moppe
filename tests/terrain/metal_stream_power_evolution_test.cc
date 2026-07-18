#include <moppe/terrain/flood.hh>
#include <moppe/terrain/metal/metal_stream_power_evolution.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  constexpr std::size_t metal_test_width = 17;
  constexpr std::size_t metal_test_height = 17;

  TerrainGrid test_grid () {
    return { .width = metal_test_width,
             .height = metal_test_height,
             .spacing_x = 20.0f * mp_units::si::metre,
             .spacing_y = 30.0f * mp_units::si::metre,
             .height_scale = 650.0f * mp_units::si::metre };
  }

  std::vector<float> test_heights () {
    std::vector<float> heights (metal_test_width * metal_test_height);
    for (std::size_t y = 0; y < metal_test_height; ++y)
      for (std::size_t x = 0; x < metal_test_width; ++x)
        heights[y * metal_test_width + x] =
          0.8f - 0.006f * static_cast<float> (x) -
          0.004f * static_cast<float> (y) +
          0.01f * std::sin (0.7f * static_cast<float> (x + y));
    return heights;
  }

  float angle_difference (float left, float right) {
    constexpr float turn = 2.0f * std::numbers::pi_v<float>;
    const float difference = std::fabs (left - right);
    return std::min (difference, turn - difference);
  }
}

MOPPE_TEST (metal_d_infinity_routes_match_the_cpu_reference) {
  const std::vector<float> heights = test_heights ();
  const TerrainView terrain (test_grid (), heights);
  const FloodField flood = analyze_standing_water (terrain, -1.0f);
  const LakeCensus census = census_lakes (flood);
  const CpuStreamPowerEvolutionBackend cpu;
  const metal::MetalStreamPowerEvolutionBackend metal (MOPPE_SHADER_ASSET_PATH);
  const FractionalDrainage expected =
    cpu.route_fractional (terrain, flood, census, {}, {});
  const FractionalDrainage actual =
    metal.route_fractional (terrain, flood, census, {}, {});

  const auto& expected_direction = spatial::get<drainage_direction> (expected);
  const auto& actual_direction = spatial::get<drainage_direction> (actual);
  const auto& expected_slope = spatial::get<terrain_slope> (expected);
  const auto& actual_slope = spatial::get<terrain_slope> (actual);
  const auto& expected_area =
    spatial::get<fractional_contributing_area> (expected);
  const auto& actual_area = spatial::get<fractional_contributing_area> (actual);
  for (std::size_t offset = 0; offset < heights.size (); ++offset) {
    const CellIndex cell { static_cast<std::uint32_t> (offset) };
    const FractionalFlowRoute& expected_route = expected.domain ().route (cell);
    const FractionalFlowRoute& actual_route = actual.domain ().route (cell);
    MOPPE_CHECK (actual_route.arc_count == expected_route.arc_count);
    for (std::uint8_t arc = 0; arc < expected_route.arc_count; ++arc) {
      MOPPE_CHECK (actual_route.arcs[arc].receiver ==
                   expected_route.arcs[arc].receiver);
      MOPPE_CHECK_NEAR (
        actual_route.arcs[arc].fraction.numerical_value_in (mp_units::one),
        expected_route.arcs[arc].fraction.numerical_value_in (mp_units::one),
        2e-5f);
    }
    MOPPE_CHECK_NEAR (meters_value (actual_route.run),
                      meters_value (expected_route.run),
                      2e-4f);
    MOPPE_CHECK (
      angle_difference (
        actual_direction[offset].numerical_value_in (mp_units::angular::radian),
        expected_direction[offset].numerical_value_in (
          mp_units::angular::radian)) < 2e-5f);
    MOPPE_CHECK_NEAR (actual_slope[offset].numerical_value_in (mp_units::one),
                      expected_slope[offset].numerical_value_in (mp_units::one),
                      2e-5f);
    MOPPE_CHECK_NEAR (actual_area[offset].numerical_value_in (
                        mp_units::si::metre * mp_units::si::metre),
                      expected_area[offset].numerical_value_in (
                        mp_units::si::metre * mp_units::si::metre),
                      0.25f);
  }
}

MOPPE_TEST (metal_hybrid_evolution_tracks_the_cpu_reference) {
  const std::vector<float> heights = test_heights ();
  const TerrainView terrain (test_grid (), heights);
  const auto zero_uplift = std::vector<meters_per_julian_year_t> (
    heights.size (),
    0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year);
  const StreamPowerEvolution parameters {
    .duration = 100000.0f * mp_units::astronomy::Julian_year,
    .time_step = 50000.0f * mp_units::astronomy::Julian_year,
    .sea_level = -1.0f
  };
  const CpuStreamPowerEvolutionBackend cpu;
  const metal::MetalStreamPowerEvolutionBackend metal (MOPPE_SHADER_ASSET_PATH);
  const StreamPowerEvolutionResult expected =
    evolve_stream_power (terrain, zero_uplift, parameters, cpu);
  const StreamPowerEvolutionResult actual =
    evolve_stream_power (terrain, zero_uplift, parameters, metal);

  for (std::size_t cell = 0; cell < heights.size (); ++cell) {
    MOPPE_CHECK_NEAR (actual.heights[cell], expected.heights[cell], 2e-5f);
    const Vec3 actual_tangent =
      actual.channel_tangents[cell].numerical_value_in (mp_units::one);
    const Vec3 expected_tangent =
      expected.channel_tangents[cell].numerical_value_in (mp_units::one);
    for (int component = 0; component < 3; ++component)
      MOPPE_CHECK_NEAR (
        actual_tangent[component], expected_tangent[component], 2e-4f);
  }
}
