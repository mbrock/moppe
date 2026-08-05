#include <moppe/terrain/sediment_transport.hh>

#include <tests/test.hh>

#include <array>
#include <cstdint>
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
