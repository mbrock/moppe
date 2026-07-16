#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  constexpr std::size_t width = 9;
  constexpr std::size_t height = 9;

  TerrainGrid plane_grid () {
    return { .width = width,
             .height = height,
             .spacing_x = 2.0f * mp_units::si::metre,
             .spacing_y = 3.0f * mp_units::si::metre,
             .height_scale = 1.0f * mp_units::si::metre };
  }

  std::vector<float> descending_plane (float direction_radians,
                                       float gradient = 0.2f) {
    const TerrainGrid grid = plane_grid ();
    const float gradient_x = gradient * std::cos (direction_radians);
    const float gradient_y = gradient * std::sin (direction_radians);
    std::vector<float> heights (width * height);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x)
        heights[y * width + x] = 100.0f - gradient_x * x * grid.spacing_x_m () -
                                 gradient_y * y * grid.spacing_y_m ();
    return heights;
  }

  FractionalDrainage analyze_plane (std::span<const float> heights) {
    const TerrainView terrain (plane_grid (), heights);
    const FloodField flood = analyze_standing_water (terrain, -1000.0f);
    return analyze_fractional_drainage (terrain, flood, census_lakes (flood));
  }
}

MOPPE_TEST (terrain_lattice_domain_omits_the_periodic_rendering_seam) {
  const TerrainLatticeDomain domain (
    { .width = 4, .height = 4, .topology = Topology::Torus });

  MOPPE_CHECK (domain.width () == 3);
  MOPPE_CHECK (domain.height () == 3);
  MOPPE_CHECK (domain.size () == 9);
  MOPPE_CHECK (domain.neighbour (CellIndex { 0 }, -1, 0) == CellIndex { 2 });
}

MOPPE_TEST (d_infinity_recovers_rotated_planar_gradients) {
  constexpr std::array degrees { 10.0f,  20.0f,  35.0f,  55.0f,  70.0f,
                                 80.0f,  100.0f, 125.0f, 160.0f, 190.0f,
                                 215.0f, 250.0f, 280.0f, 305.0f, 340.0f };
  constexpr CellIndex center { 4 + 4 * width };
  for (const float degree : degrees) {
    const float expected = degree * std::numbers::pi_v<float> / 180.0f;
    const std::vector<float> heights = descending_plane (expected);
    const FractionalDrainage drainage = analyze_plane (heights);
    const auto& directions = spatial::get<drainage_direction> (drainage);
    const auto& slopes = spatial::get<terrain_slope> (drainage);
    const FractionalFlowRoute& route = drainage.domain ().route (center);

    MOPPE_CHECK (route.arc_count == 2);
    MOPPE_CHECK_NEAR (
      directions[center.value].numerical_value_in (mp_units::angular::radian),
      expected,
      2e-5f);
    MOPPE_CHECK_NEAR (
      slopes[center.value].numerical_value_in (mp_units::one), 0.2f, 1e-5f);
    const float total =
      route.arcs[0].fraction.numerical_value_in (mp_units::one) +
      route.arcs[1].fraction.numerical_value_in (mp_units::one);
    MOPPE_CHECK_NEAR (total, 1.0f, 1e-6f);
  }
}

MOPPE_TEST (d_infinity_keeps_geometric_interpolation_distinct_from_flow_split) {
  const float direction = std::atan2 (1.0f, 3.0f);
  const std::vector<float> heights = descending_plane (direction);
  const FractionalDrainage drainage = analyze_plane (heights);
  constexpr CellIndex center { 4 + 4 * width };
  const FractionalFlowRoute& route = drainage.domain ().route (center);

  MOPPE_CHECK (route.arc_count == 2);
  const float angular_split =
    route.arcs[1].fraction.numerical_value_in (mp_units::one);
  const float interpolation =
    route.receiver_interpolation.numerical_value_in (mp_units::one);
  const float facet_extent =
    std::atan2 (plane_grid ().spacing_y_m (), plane_grid ().spacing_x_m ());
  MOPPE_CHECK_NEAR (angular_split, direction / facet_extent, 2e-5f);
  MOPPE_CHECK_NEAR (interpolation, 2.0f / 9.0f, 2e-5f);
  MOPPE_CHECK (std::fabs (angular_split - interpolation) > 0.05f);
}

MOPPE_TEST (fractional_accumulation_is_conservative_acyclic_and_deterministic) {
  const std::vector<float> heights = descending_plane (0.37f);
  const FractionalDrainage first = analyze_plane (heights);
  const FractionalDrainage second = analyze_plane (heights);
  const auto first_order = first.domain ().topological_order ();
  const auto second_order = second.domain ().topological_order ();

  MOPPE_CHECK (first_order.size () == width * height);
  MOPPE_CHECK (std::equal (
    first_order.begin (), first_order.end (), second_order.begin ()));

  std::vector<std::size_t> position (width * height);
  for (std::size_t i = 0; i < first_order.size (); ++i)
    position[first_order[i].value] = i;

  const auto& first_area = spatial::get<fractional_contributing_area> (first);
  const auto& second_area = spatial::get<fractional_contributing_area> (second);
  double outlet_area_m2 = 0.0;
  for (std::size_t offset = 0; offset < width * height; ++offset) {
    const CellIndex cell { static_cast<std::uint32_t> (offset) };
    const FractionalFlowRoute& route = first.domain ().route (cell);
    for (std::uint8_t arc = 0; arc < route.arc_count; ++arc)
      MOPPE_CHECK (position[cell.value] <
                   position[route.arcs[arc].receiver.value]);
    if (route.empty ())
      outlet_area_m2 += first_area[offset].numerical_value_in (
        mp_units::si::metre * mp_units::si::metre);
    MOPPE_CHECK_NEAR (first_area[offset].numerical_value_in (
                        mp_units::si::metre * mp_units::si::metre),
                      second_area[offset].numerical_value_in (
                        mp_units::si::metre * mp_units::si::metre),
                      0.0f);
  }
  MOPPE_CHECK_NEAR (static_cast<float> (outlet_area_m2),
                    static_cast<float> (width * height) *
                      square_meters_value (plane_grid ().cell_area ()),
                    1e-3f);
}

MOPPE_TEST (fractional_accumulation_materializes_an_area_weighted_tangent) {
  const std::vector<float> heights = descending_plane (0.37f);
  const FractionalDrainage drainage = analyze_plane (heights);
  const auto& areas = spatial::get<fractional_contributing_area> (drainage);
  const auto& tangents = spatial::get<channel_tangent> (drainage);
  const auto& fluxes = spatial::get<channel_area_flux> (drainage);

  for (std::size_t offset = 0; offset < width * height; ++offset) {
    const Vec3 tangent = tangents[offset].numerical_value_in (mp_units::one);
    const Vec3 flux = fluxes[offset].numerical_value_in (mp_units::si::metre *
                                                         mp_units::si::metre);
    const float area = areas[offset].numerical_value_in (mp_units::si::metre *
                                                         mp_units::si::metre);
    MOPPE_CHECK_NEAR (length (tangent), 1.0f, 1e-5f);
    MOPPE_CHECK_NEAR (length (flux), area, 1e-3f);
    MOPPE_CHECK_NEAR (tangent[1], 0.0f, 0.0f);
  }
}

MOPPE_TEST (channel_memory_can_select_an_aligned_downhill_route) {
  TerrainGrid grid = plane_grid ();
  std::vector<float> heights (width * height, 20.0f);
  constexpr std::size_t center_x = 4;
  constexpr std::size_t center_y = 4;
  constexpr CellIndex center { center_x + center_y * width };
  heights[center.value] = 10.0f;
  for (std::size_t x = center_x + 1; x < width; ++x)
    heights[center_y * width + x] = 10.0f - (x - center_x);
  for (std::size_t y = 0; y < center_y; ++y)
    heights[y * width + center_x] =
      10.0f - 0.9f * static_cast<float> (center_y - y);

  const TerrainView terrain (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, -1000.0f);
  const LakeCensus census = census_lakes (flood);
  const FractionalDrainage memoryless =
    analyze_fractional_drainage (terrain, flood, census);
  std::vector<ChannelTangent> memory (width * height,
                                      Vec3 () * channel_tangent[mp_units::one]);
  memory[center.value] =
    Vec3 (0.0f, 0.0f, -1.0f) * channel_tangent[mp_units::one];
  const FractionalDrainage persistent = analyze_fractional_drainage (
    terrain, flood, census, memory, 0.9f * channel_persistence[mp_units::one]);

  MOPPE_CHECK (memoryless.domain ().route (center).arcs[0].receiver ==
               CellIndex { center.value + 1 });
  MOPPE_CHECK (persistent.domain ().route (center).arcs[0].receiver ==
               CellIndex { center.value - width });
  MOPPE_CHECK (heights[center.value - width] < heights[center.value]);
}
