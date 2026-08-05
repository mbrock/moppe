#include <moppe/terrain/sediment_transport.hh>

#include <tests/test.hh>

#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  constexpr auto cubic_metre = u::m * u::m * u::m;

  SedimentVolume test_sediment_volume (double cubic_metres) {
    return cubic_metres * sediment_volume[cubic_metre];
  }

  double test_sediment_value (SedimentVolume sediment) {
    return sediment.numerical_value_in (cubic_metre);
  }

  double test_balance_value (cubic_meters_f64_t balance) {
    return balance.numerical_value_in (cubic_metre);
  }

  FractionalFlowRoute route_to (std::uint32_t receiver) {
    return { .arcs = { FractionalFlowArc {
               .receiver = CellIndex { receiver },
               .fraction = 1.0f * flow_fraction[mp_units::one] } },
             .arc_count = 1 };
  }

  FractionalFlowRoute
  split_to (std::uint32_t first, float first_share, std::uint32_t second) {
    return { .arcs = { FractionalFlowArc { .receiver = CellIndex { first },
                                           .fraction =
                                             first_share *
                                             flow_fraction[mp_units::one] },
                       FractionalFlowArc { .receiver = CellIndex { second },
                                           .fraction =
                                             (1.0f - first_share) *
                                             flow_fraction[mp_units::one] } },
             .arc_count = 2 };
  }

  FractionalFlowDomain flow_domain (std::vector<FractionalFlowRoute> routes,
                                    std::vector<CellIndex> order) {
    return FractionalFlowDomain (
      TerrainCellDomain (TerrainDomain (2, routes.size () / 2)),
      std::move (routes),
      std::move (order));
  }

  std::vector<SurfaceElevation>
  hillslope_heights (std::span<const float> values) {
    std::vector<SurfaceElevation> heights;
    heights.reserve (values.size ());
    for (float value : values)
      heights.push_back (surface_elevation_point (value * u::m));
    return heights;
  }

  std::vector<SedimentThickness> hillslope_cover (std::size_t count,
                                                  float thickness_m = 0.0f) {
    return std::vector<SedimentThickness> (
      count, thickness_m * sediment_thickness[u::m]);
  }

  HillslopeTransportResult
  hillslope_transport (const TerrainDomain& domain,
                       std::span<const float> heights,
                       std::span<const SedimentThickness> cover,
                       std::span<const std::uint8_t> fixed,
                       float years = 0.1f) {
    const auto elevations = hillslope_heights (heights);
    return route_hillslope_sediment (domain,
                                     elevations,
                                     cover,
                                     fixed,
                                     years * mp_units::astronomy::Julian_year,
                                     0.1f * u::m * u::m /
                                       mp_units::astronomy::Julian_year);
  }
}

MOPPE_TEST (hillslope_face_transfers_close_the_solid_volume_ledger) {
  const TerrainDomain domain (3, 3, 1.0f * u::m, 1.0f * u::m);
  const std::array heights { 0.0f, 0.0f, 0.0f, 0.0f, 10.0f,
                             0.0f, 0.0f, 0.0f, 0.0f };
  const auto cover = hillslope_cover (heights.size ());
  const std::array<std::uint8_t, 9> fixed {};
  const HillslopeTransportResult result =
    hillslope_transport (domain, heights, cover, fixed);

  double initial_volume = 0.0;
  double final_volume = 0.0;
  double eroded_volume = 0.0;
  double deposited_volume = 0.0;
  for (std::size_t cell = 0; cell < heights.size (); ++cell) {
    initial_volume += heights[cell];
    final_volume += surface_elevation_value (result.heights[cell]);
    eroded_volume += result.eroded_thickness[cell].numerical_value_in (u::m);
    deposited_volume +=
      result.deposited_thickness[cell].numerical_value_in (u::m);
  }
  MOPPE_CHECK_NEAR (static_cast<float> (final_volume),
                    static_cast<float> (initial_volume),
                    0.0f);
  MOPPE_CHECK_NEAR (static_cast<float> (eroded_volume),
                    static_cast<float> (deposited_volume),
                    0.0f);
  MOPPE_CHECK_NEAR (
    static_cast<float> (test_balance_value (result.balance_residual)),
    0.0f,
    1e-7f);
}

MOPPE_TEST (hillslope_fixed_cells_form_a_no_flux_boundary) {
  const TerrainDomain domain (3, 3, 1.0f * u::m, 1.0f * u::m);
  const std::array heights { 0.0f, 0.0f, 0.0f, 0.0f, 10.0f,
                             0.0f, 0.0f, 0.0f, 0.0f };
  const auto cover = hillslope_cover (heights.size ());
  const std::array<std::uint8_t, 9> fixed { 1, 1, 1, 1, 0, 1, 1, 1, 1 };
  const HillslopeTransportResult result =
    hillslope_transport (domain, heights, cover, fixed);

  for (std::size_t cell = 0; cell < heights.size (); ++cell)
    MOPPE_CHECK_NEAR (
      surface_elevation_value (result.heights[cell]), heights[cell], 0.0f);
  MOPPE_CHECK (result.transferred == SedimentVolume::zero ());
}

MOPPE_TEST (hillslope_transport_moves_cover_before_detaching_bedrock) {
  const TerrainDomain domain (3, 3, 1.0f * u::m, 1.0f * u::m);
  const std::array heights { 0.0f, 0.0f, 0.0f, 0.0f, 10.0f,
                             0.0f, 0.0f, 0.0f, 0.0f };
  auto cover = hillslope_cover (heights.size ());
  cover[4] = 1.0f * sediment_thickness[u::m];
  const std::array<std::uint8_t, 9> fixed {};
  const HillslopeTransportResult covered =
    hillslope_transport (domain, heights, cover, fixed);
  const HillslopeTransportResult bare = hillslope_transport (
    domain, heights, hillslope_cover (heights.size ()), fixed);

  MOPPE_CHECK (covered.bedrock_detached == SedimentVolume::zero ());
  MOPPE_CHECK (test_sediment_value (bare.bedrock_detached) > 0.0);
  MOPPE_CHECK (covered.sediment_thickness[4] < cover[4]);
  MOPPE_CHECK (covered.sediment_thickness[1] > SedimentThickness::zero ());
  double covered_total_m = 0.0;
  double bare_total_m = 0.0;
  for (std::size_t cell = 0; cell < heights.size (); ++cell) {
    covered_total_m +=
      covered.sediment_thickness[cell].numerical_value_in (u::m);
    bare_total_m += bare.sediment_thickness[cell].numerical_value_in (u::m);
  }
  MOPPE_CHECK_NEAR (static_cast<float> (covered_total_m), 1.0f, 1e-7f);
  MOPPE_CHECK_NEAR (
    static_cast<float> (bare_total_m),
    static_cast<float> (test_sediment_value (bare.bedrock_detached)),
    1e-7f);
}

MOPPE_TEST (hillslope_transport_rounds_convexities_and_fills_concavities) {
  const TerrainDomain domain (3, 3, 1.0f * u::m, 1.0f * u::m);
  const std::array peak {
    0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f
  };
  const std::array basin { 10.0f, 10.0f, 10.0f, 10.0f, 0.0f,
                           10.0f, 10.0f, 10.0f, 10.0f };
  const auto cover = hillslope_cover (peak.size ());
  const std::array<std::uint8_t, 9> fixed {};
  const HillslopeTransportResult rounded =
    hillslope_transport (domain, peak, cover, fixed);
  const HillslopeTransportResult filled =
    hillslope_transport (domain, basin, cover, fixed);

  MOPPE_CHECK (surface_elevation_value (rounded.heights[4]) < peak[4]);
  MOPPE_CHECK (surface_elevation_value (rounded.heights[1]) > peak[1]);
  MOPPE_CHECK (surface_elevation_value (filled.heights[4]) > basin[4]);
  MOPPE_CHECK (surface_elevation_value (filled.heights[1]) < basin[1]);
}

MOPPE_TEST (hillslope_transport_substeps_long_geological_intervals) {
  const TerrainDomain domain (3, 3, 1.0f * u::m, 1.0f * u::m);
  const std::array heights { 0.0f, 0.0f, 0.0f, 0.0f, 10.0f,
                             0.0f, 0.0f, 0.0f, 0.0f };
  const auto cover = hillslope_cover (heights.size ());
  const std::array<std::uint8_t, 9> fixed {};
  const auto elevations = hillslope_heights (heights);
  const HillslopeTransportResult result = route_hillslope_sediment (
    domain,
    elevations,
    cover,
    fixed,
    100.0f * mp_units::astronomy::Julian_year,
    0.1f * u::m * u::m / mp_units::astronomy::Julian_year);

  MOPPE_CHECK (count_value (result.sweeps) >= 40);
  for (const SurfaceElevation elevation : result.heights)
    MOPPE_CHECK (std::isfinite (surface_elevation_value (elevation)));
  MOPPE_CHECK_NEAR (
    static_cast<float> (test_balance_value (result.balance_residual)),
    0.0f,
    1e-5f);
}

MOPPE_TEST (fluvial_capacity_is_typed_discharge_times_slope_and_time) {
  const FluvialTransport transport {
    .runoff_rate = 2.0f * u::m / mp_units::astronomy::Julian_year,
    .concentration_at_unit_slope =
      0.01f * sediment_concentration[mp_units::one],
  };
  const auto capacity = [&] (float years) {
    return sediment_transport_capacity (years *
                                          mp_units::astronomy::Julian_year,
                                        10.0f * u::m * u::m,
                                        0.5f * terrain_slope[mp_units::one],
                                        0.25f,
                                        transport);
  };

  MOPPE_CHECK_NEAR (test_sediment_value (capacity (100.0f)), 2.5f, 1e-6f);
  MOPPE_CHECK_NEAR (test_sediment_value (capacity (200.0f)), 5.0f, 1e-6f);
}

MOPPE_TEST (fluvial_transport_entrains_cover_before_cutting_bedrock) {
  const FractionalFlowDomain flow = flow_domain (
    { route_to (1), {}, {}, {} },
    { CellIndex { 0 }, CellIndex { 1 }, CellIndex { 2 }, CellIndex { 3 } });
  const std::array potential { test_sediment_volume (10.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0) };
  const std::array deposition_limit { test_sediment_volume (20.0),
                                      test_sediment_volume (20.0),
                                      test_sediment_volume (20.0),
                                      test_sediment_volume (20.0) };
  const std::array cover { test_sediment_volume (8.0),
                           test_sediment_volume (0.0),
                           test_sediment_volume (0.0),
                           test_sediment_volume (0.0) };
  const std::array<std::uint8_t, 4> ocean { 0, 1, 1, 1 };
  const auto route_at_capacity = [&] (double capacity_m3) {
    const std::array capacity { test_sediment_volume (capacity_m3),
                                test_sediment_volume (0.0),
                                test_sediment_volume (0.0),
                                test_sediment_volume (0.0) };
    return route_sediment (
      flow, potential, capacity, deposition_limit, ocean, cover);
  };

  const SedimentRoutingResult protected_bedrock = route_at_capacity (5.0);
  MOPPE_CHECK_NEAR (
    test_sediment_value (protected_bedrock.entrained_cover[0]), 5.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    test_sediment_value (protected_bedrock.bedrock_detached[0]), 0.0f, 0.0f);

  const SedimentRoutingResult exposed_bedrock = route_at_capacity (12.0);
  MOPPE_CHECK_NEAR (
    test_sediment_value (exposed_bedrock.entrained_cover[0]), 8.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    test_sediment_value (exposed_bedrock.bedrock_detached[0]), 4.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    test_sediment_value (exposed_bedrock.exported_to_ocean), 12.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    test_balance_value (exposed_bedrock.balance_residual), 0.0f, 0.0f);
}

MOPPE_TEST (sediment_routes_from_source_to_ocean_without_loss) {
  const FractionalFlowDomain flow = flow_domain (
    { route_to (1), route_to (2), {}, {} },
    { CellIndex { 0 }, CellIndex { 1 }, CellIndex { 2 }, CellIndex { 3 } });
  const std::array potential { test_sediment_volume (10.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0) };
  const std::array capacity { test_sediment_volume (20.0),
                              test_sediment_volume (20.0),
                              test_sediment_volume (0.0),
                              test_sediment_volume (0.0) };
  const std::array deposition_limit { test_sediment_volume (20.0),
                                      test_sediment_volume (20.0),
                                      test_sediment_volume (20.0),
                                      test_sediment_volume (20.0) };
  const std::array<std::uint8_t, 4> ocean { 0, 0, 1, 1 };

  const SedimentRoutingResult result =
    route_sediment (flow, potential, capacity, deposition_limit, ocean);

  MOPPE_CHECK_NEAR (test_sediment_value (result.detached_total), 10.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_sediment_value (result.deposited_total), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    test_sediment_value (result.exported_to_ocean), 10.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_balance_value (result.balance_residual), 0.0f, 0.0f);
}

MOPPE_TEST (sediment_deposits_where_transport_capacity_falls) {
  const FractionalFlowDomain flow = flow_domain (
    { route_to (1), route_to (2), {}, {} },
    { CellIndex { 0 }, CellIndex { 1 }, CellIndex { 2 }, CellIndex { 3 } });
  const std::array potential { test_sediment_volume (10.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0) };
  const std::array capacity { test_sediment_volume (20.0),
                              test_sediment_volume (3.0),
                              test_sediment_volume (0.0),
                              test_sediment_volume (0.0) };
  const std::array deposition_limit { test_sediment_volume (20.0),
                                      test_sediment_volume (20.0),
                                      test_sediment_volume (20.0),
                                      test_sediment_volume (20.0) };
  const std::array<std::uint8_t, 4> ocean { 0, 0, 1, 1 };

  const SedimentRoutingResult result =
    route_sediment (flow, potential, capacity, deposition_limit, ocean);

  MOPPE_CHECK_NEAR (test_sediment_value (result.deposited[1]), 7.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_sediment_value (result.exported_to_ocean), 3.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_balance_value (result.balance_residual), 0.0f, 0.0f);
}

MOPPE_TEST (sediment_aggradation_limit_carries_excess_farther_downstream) {
  const FractionalFlowDomain flow = flow_domain (
    { route_to (1), route_to (2), route_to (3), {} },
    { CellIndex { 0 }, CellIndex { 1 }, CellIndex { 2 }, CellIndex { 3 } });
  const std::array potential { test_sediment_volume (10.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0) };
  const std::array capacity { test_sediment_volume (10.0),
                              test_sediment_volume (0.0),
                              test_sediment_volume (0.0),
                              test_sediment_volume (0.0) };
  const std::array deposition_limit { test_sediment_volume (2.0),
                                      test_sediment_volume (2.0),
                                      test_sediment_volume (2.0),
                                      test_sediment_volume (2.0) };
  const std::array<std::uint8_t, 4> ocean { 0, 0, 0, 1 };

  const SedimentRoutingResult result =
    route_sediment (flow, potential, capacity, deposition_limit, ocean);

  MOPPE_CHECK_NEAR (test_sediment_value (result.deposited[1]), 2.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_sediment_value (result.deposited[2]), 2.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_sediment_value (result.exported_to_ocean), 6.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_balance_value (result.balance_residual), 0.0f, 0.0f);
}

MOPPE_TEST (sediment_confluence_and_fractional_split_conserve_volume) {
  const FractionalFlowDomain flow = flow_domain ({ route_to (2),
                                                   route_to (2),
                                                   split_to (3, 0.25f, 4),
                                                   route_to (5),
                                                   route_to (5),
                                                   {} },
                                                 { CellIndex { 0 },
                                                   CellIndex { 1 },
                                                   CellIndex { 2 },
                                                   CellIndex { 3 },
                                                   CellIndex { 4 },
                                                   CellIndex { 5 } });
  const std::array potential {
    test_sediment_volume (4.0), test_sediment_volume (6.0),
    test_sediment_volume (0.0), test_sediment_volume (0.0),
    test_sediment_volume (0.0), test_sediment_volume (0.0)
  };
  const std::array capacity {
    test_sediment_volume (10.0), test_sediment_volume (10.0),
    test_sediment_volume (10.0), test_sediment_volume (1.0),
    test_sediment_volume (10.0), test_sediment_volume (0.0)
  };
  const std::array deposition_limit {
    test_sediment_volume (20.0), test_sediment_volume (20.0),
    test_sediment_volume (20.0), test_sediment_volume (20.0),
    test_sediment_volume (20.0), test_sediment_volume (20.0)
  };
  const std::array<std::uint8_t, 6> ocean { 0, 0, 0, 0, 0, 1 };

  const SedimentRoutingResult result =
    route_sediment (flow, potential, capacity, deposition_limit, ocean);

  MOPPE_CHECK_NEAR (test_sediment_value (result.deposited[3]), 1.5f, 0.0f);
  MOPPE_CHECK_NEAR (test_sediment_value (result.exported_to_ocean), 8.5f, 0.0f);
  MOPPE_CHECK_NEAR (test_balance_value (result.balance_residual), 0.0f, 0.0f);
}

MOPPE_TEST (sediment_non_ocean_sink_retains_incoming_material) {
  const FractionalFlowDomain flow = flow_domain (
    { route_to (1), {}, {}, {} },
    { CellIndex { 0 }, CellIndex { 1 }, CellIndex { 2 }, CellIndex { 3 } });
  const std::array potential { test_sediment_volume (5.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0),
                               test_sediment_volume (0.0) };
  const std::array capacity { test_sediment_volume (5.0),
                              test_sediment_volume (0.0),
                              test_sediment_volume (0.0),
                              test_sediment_volume (0.0) };
  const std::array deposition_limit { test_sediment_volume (20.0),
                                      test_sediment_volume (20.0),
                                      test_sediment_volume (20.0),
                                      test_sediment_volume (20.0) };
  const std::array<std::uint8_t, 4> ocean { 0, 0, 0, 0 };

  const SedimentRoutingResult result =
    route_sediment (flow, potential, capacity, deposition_limit, ocean);

  MOPPE_CHECK_NEAR (test_sediment_value (result.deposited[1]), 5.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_sediment_value (result.exported_to_ocean), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (test_balance_value (result.balance_residual), 0.0f, 0.0f);
}
