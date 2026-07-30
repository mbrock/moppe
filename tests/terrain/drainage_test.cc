#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace moppe::terrain;

namespace {
  CellIndex receiver_root (const DrainageGraph& graph, CellIndex origin) {
    CellIndex cell = origin;
    for (std::size_t steps = 0; steps < graph.receiver.size (); ++steps) {
      const CellIndex next = graph.receiver[cell];
      if (next == cell)
        return cell;
      cell = next;
    }
    return no_cell;
  }

  const RiverReach* drainage_reach_containing (const RiverNetwork& network,
                                               CellIndex cell) {
    for (const RiverReach& reach : network.reaches)
      if (std::ranges::find (reach.cells, cell) != reach.cells.end ())
        return &reach;
    return nullptr;
  }
}

MOPPE_TEST (d8_drainage_routes_to_the_steepest_lower_neighbor) {
  const std::array heights { 30.0f, 20.0f, 30.0f, 20.0f, 0.0f,
                             20.0f, 30.0f, 20.0f, 30.0f };
  const ElevationMap terrain = make_elevation_map (
    TerrainDomain (
      3, 3, 2.0f * mp_units::si::metre, 2.0f * mp_units::si::metre),
    heights);
  const DrainageGraph graph = analyze_drainage (terrain);

  for (std::uint32_t cell = 0; cell < 9; ++cell)
    MOPPE_CHECK (graph.receiver[cell] == 4);
  MOPPE_CHECK_NEAR (graph.contributing_area_at (4).numerical_value_in (
                      mp_units::si::metre * mp_units::si::metre),
                    36.0f,
                    0.0f);
  MOPPE_CHECK_NEAR (graph.slope_at (0).numerical_value_in (mp_units::one),
                    30.0f / std::sqrt (8.0f),
                    1e-6f);
}

MOPPE_TEST (periodic_drainage_crosses_the_wrap) {
  // Cell (0,0) reaches the low point at (2,0) by crossing the wrap.
  const std::array heights { 2.0f, 3.0f, 0.0f, 4.0f, 4.0f,
                             4.0f, 4.0f, 4.0f, 4.0f };
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (3, 3), heights);
  const DrainageGraph graph = analyze_drainage (terrain);

  MOPPE_CHECK (graph.width () == 3);
  MOPPE_CHECK (graph.height () == 3);
  MOPPE_CHECK (graph.receiver[0] == 2);
  MOPPE_CHECK (receiver_root (graph, 0) == 2);
}

MOPPE_TEST (wet_drainage_carries_a_catchment_across_a_lake) {
  const std::array heights { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 4.f, 2.f, 3.f,
                             0.f, 0.f, 5.f, 1.f, 4.f, 0.f, 0.f, 6.f, 5.f,
                             5.f, 0.f, 0.f, 6.f, 6.f, 6.f, 0.f };
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (5, 5), heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph dry = analyze_drainage (terrain);
  const DrainageGraph wet = analyze_wet_drainage (flood, census);

  MOPPE_CHECK (dry.receiver[12] == 12);
  MOPPE_CHECK (wet.receiver[12] != 12);
  MOPPE_CHECK (receiver_root (wet, 12) != no_cell);

  const WaterBody& lake = census.water_bodies ()[0];
  std::size_t exits = 0;
  for (std::uint32_t member = 0; member < census.cell_count (); ++member)
    if (census.body_at (CellIndex { member }) == lake.id &&
        census.body_at (wet.receiver[member]) != lake.id)
      ++exits;
  MOPPE_CHECK (exits == 1);
  MOPPE_CHECK (wet.receiver[lake.outlet_cell] == lake.spill_cell);

  const RiverNetwork rivers = extract_river_network (
    flood, census, wet, 1.0f * mp_units::si::metre * mp_units::si::metre);
  bool enters_lake = false;
  bool leaves_lake = false;
  bool links_across_lake = false;
  for (const RiverReach& reach : rivers.reaches) {
    enters_lake |= reach.downstream_body == lake.id;
    leaves_lake |= reach.upstream_body == lake.id;
    if (reach.downstream_body == lake.id &&
        reach.downstream_reach != RiverReach::no_id) {
      links_across_lake = true;
      const RiverReach& downstream = rivers.reaches[reach.downstream_reach];
      MOPPE_CHECK (!reach.alignment.points.empty ());
      MOPPE_CHECK (!downstream.alignment.points.empty ());
      const RiverAlignmentPoint& inlet = reach.alignment.points.back ();
      const RiverAlignmentPoint& spill = downstream.alignment.points.front ();
      MOPPE_CHECK_NEAR (inlet.x_m, spill.x_m, 1e-5f);
      MOPPE_CHECK_NEAR (inlet.z_m, spill.z_m, 1e-5f);
      MOPPE_CHECK_NEAR (inlet.flow_distance_m, spill.flow_distance_m, 1e-5f);
    }
    for (const std::uint32_t member : reach.cells) {
      MOPPE_CHECK (census.body_at (CellIndex { member }) == LakeCensus::dry);
      MOPPE_CHECK (!flood.ocean[member]);
    }
  }
  MOPPE_CHECK (enters_lake);
  MOPPE_CHECK (leaves_lake);
  MOPPE_CHECK (links_across_lake);
  MOPPE_CHECK (!drainage_reach_containing (rivers, lake.outlet_cell));

  std::uint32_t cell = 12;
  std::size_t steps = 0;
  while (wet.receiver[cell] != cell && steps < heights.size ()) {
    cell = wet.receiver[cell];
    ++steps;
  }
  MOPPE_CHECK (steps < heights.size ());
  MOPPE_CHECK_NEAR (
    wet.contributing_area_at (cell).numerical_value_in (u::m * u::m),
    25.0f,
    0.0f);
}

MOPPE_TEST (wet_drainage_preserves_steepest_descent_on_dry_ground) {
  const std::array heights { 5.f, 4.f, 3.f, 4.f, 2.f, 1.f, 3.f, 1.f, 0.f };
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (3, 3), heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph dry = analyze_drainage (terrain);
  const WetDrainageRouting routing = route_wet_drainage (flood, census);
  const DrainageGraph wet = analyze_wet_drainage (flood, census);

  MOPPE_CHECK (routing.receiver == wet.receiver);
  MOPPE_CHECK (wet.receiver == dry.receiver);
  for (std::size_t cell = 0; cell < heights.size (); ++cell) {
    MOPPE_CHECK_NEAR (routing.slope[cell],
                      wet.slope_at (cell).numerical_value_in (mp_units::one),
                      0.0f);
    MOPPE_CHECK_NEAR (
      wet.contributing_area_at (cell).numerical_value_in (u::m * u::m),
      dry.contributing_area_at (cell).numerical_value_in (u::m * u::m),
      0.0f);
    MOPPE_CHECK (
      receiver_root (wet, CellIndex { static_cast<std::uint32_t> (cell) }) !=
      no_cell);
  }
}

MOPPE_TEST (wet_drainage_and_river_network_are_deterministic) {
  const std::array heights { 0.f, 4.f, 3.f, 2.f, 1.f, 5.f, 3.f, 2.f, 4.f };
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (3, 3), heights);
  const FloodField flood_a = analyze_standing_water (terrain, 0.0f);
  const FloodField flood_b = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census_a = census_lakes (flood_a);
  const LakeCensus census_b = census_lakes (flood_b);
  const DrainageGraph graph_a = analyze_wet_drainage (flood_a, census_a);
  const DrainageGraph graph_b = analyze_wet_drainage (flood_b, census_b);
  const RiverNetwork rivers_a =
    extract_river_network (flood_a,
                           census_a,
                           graph_a,
                           1.0f * mp_units::si::metre * mp_units::si::metre);
  const RiverNetwork rivers_b =
    extract_river_network (flood_b,
                           census_b,
                           graph_b,
                           1.0f * mp_units::si::metre * mp_units::si::metre);

  MOPPE_CHECK (flood_a.spill_receiver == flood_b.spill_receiver);
  MOPPE_CHECK (graph_a.receiver == graph_b.receiver);
  MOPPE_CHECK (rivers_a.reaches.size () == rivers_b.reaches.size ());
  MOPPE_CHECK (rivers_a.waterfalls.size () == rivers_b.waterfalls.size ());
  for (std::size_t i = 0; i < graph_a.contributing_areas ().size (); ++i)
    MOPPE_CHECK_NEAR (
      graph_a.contributing_area_at (i).numerical_value_in (u::m * u::m),
      graph_b.contributing_area_at (i).numerical_value_in (u::m * u::m),
      0.0f);
  for (std::size_t i = 0; i < rivers_a.reaches.size (); ++i) {
    const RiverReach& a = rivers_a.reaches[i];
    const RiverReach& b = rivers_b.reaches[i];
    MOPPE_CHECK (a.cells == b.cells);
    MOPPE_CHECK (a.upstream_body == b.upstream_body);
    MOPPE_CHECK (a.downstream_body == b.downstream_body);
    MOPPE_CHECK (a.downstream_ocean == b.downstream_ocean);
    MOPPE_CHECK (a.downstream_reach == b.downstream_reach);
    MOPPE_CHECK (a.alignment.points.size () == b.alignment.points.size ());
    for (std::size_t point = 0; point < a.alignment.points.size (); ++point) {
      const RiverAlignmentPoint& point_a = a.alignment.points[point];
      const RiverAlignmentPoint& point_b = b.alignment.points[point];
      MOPPE_CHECK_NEAR (point_a.x_m, point_b.x_m, 0.0f);
      MOPPE_CHECK_NEAR (point_a.z_m, point_b.z_m, 0.0f);
      MOPPE_CHECK_NEAR (point_a.flow_distance_m, point_b.flow_distance_m, 0.0f);
      MOPPE_CHECK_NEAR (
        point_a.contributing_area_m2, point_b.contributing_area_m2, 0.0f);
      MOPPE_CHECK_NEAR (point_a.water_level_m, point_b.water_level_m, 0.0f);
      MOPPE_CHECK_NEAR (point_a.standing_water, point_b.standing_water, 0.0f);
    }
    MOPPE_CHECK_NEAR (
      (a.downstream_area).numerical_value_in (moppe::u::m * moppe::u::m),
      (b.downstream_area).numerical_value_in (moppe::u::m * moppe::u::m),
      0.0f);
    MOPPE_CHECK_NEAR (a.maximum_slope.numerical_value_in (mp_units::one),
                      b.maximum_slope.numerical_value_in (mp_units::one),
                      0.0f);
  }
  for (std::size_t i = 0; i < rivers_a.waterfalls.size (); ++i) {
    const Waterfall& a = rivers_a.waterfalls[i];
    const Waterfall& b = rivers_b.waterfalls[i];
    MOPPE_CHECK (a.lip_cell == b.lip_cell);
    MOPPE_CHECK (a.foot_cell == b.foot_cell);
    MOPPE_CHECK (a.reach_id == b.reach_id);
    MOPPE_CHECK_NEAR ((a.drop).numerical_value_in (moppe::u::m),
                      (b.drop).numerical_value_in (moppe::u::m),
                      0.0f);
    MOPPE_CHECK_NEAR (
      (a.contributing_area).numerical_value_in (moppe::u::m * moppe::u::m),
      (b.contributing_area).numerical_value_in (moppe::u::m * moppe::u::m),
      0.0f);
  }
}

MOPPE_TEST (channel_refined_extraction_keeps_topology_and_bends_geometry) {
  // A plane tilted between east and southeast: D8 routes due east while the
  // true descent (and thus the D-infinity channel tangent) points ~22 degrees
  // south of east. The refined extraction must keep the D8 reach topology
  // exactly and only re-derive the continuous alignment geometry from the
  // fractional columns.
  constexpr std::size_t width = 8;
  constexpr std::size_t height = 8;
  std::vector<float> heights (width * height);
  for (std::size_t y = 0; y < height; ++y)
    for (std::size_t x = 0; x < width; ++x)
      heights[y * width + x] =
        30.0f - static_cast<float> (x) - 0.4f * static_cast<float> (y);
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (width, height), heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph wet = analyze_wet_drainage (flood, census);
  const FractionalDrainage channels =
    analyze_fractional_drainage (flood, census);
  const auto minimum_area = 0.5f * mp_units::si::metre * mp_units::si::metre;
  const RiverNetwork base =
    extract_river_network (flood, census, wet, minimum_area);
  const RiverNetwork refined =
    extract_river_network (flood, census, wet, channels, minimum_area);

  MOPPE_CHECK (refined.reaches.size () == base.reaches.size ());

  const auto& fractional_areas =
    moppe::spatial::get<fractional_contributing_area> (channels);
  bool area_differs = false;
  bool interior_bends = false;
  for (std::size_t id = 0; id < refined.reaches.size (); ++id) {
    const RiverReach& reach = refined.reaches[id];
    const RiverReach& reference = base.reaches[id];
    MOPPE_CHECK (reach.cells == reference.cells);
    MOPPE_CHECK (reach.downstream_reach == reference.downstream_reach);
    MOPPE_CHECK (!reach.alignment.points.empty ());

    // Knots at the routed cells are exact, so the source point carries the
    // fractional contributing area of its cell verbatim.
    MOPPE_CHECK_NEAR (
      reach.alignment.points.front ().contributing_area_m2,
      fractional_areas[reach.cells.front ()].numerical_value_in (
        mp_units::si::metre * mp_units::si::metre),
      1e-4f);
    MOPPE_CHECK_NEAR (reach.alignment.points.front ().x_m,
                      reference.alignment.points.front ().x_m,
                      1e-5f);
    MOPPE_CHECK_NEAR (reach.alignment.points.front ().z_m,
                      reference.alignment.points.front ().z_m,
                      1e-5f);
    MOPPE_CHECK_NEAR (reach.alignment.points.back ().x_m,
                      reference.alignment.points.back ().x_m,
                      1e-5f);
    MOPPE_CHECK_NEAR (reach.alignment.points.back ().z_m,
                      reference.alignment.points.back ().z_m,
                      1e-5f);
    for (const RiverAlignmentPoint& point : reach.alignment.points) {
      MOPPE_CHECK (std::isfinite (point.x_m) && std::isfinite (point.z_m));
      MOPPE_CHECK (point.contributing_area_m2 > 0.0f);
    }
    area_differs |=
      std::fabs (reach.alignment.points.front ().contributing_area_m2 -
                 reference.alignment.points.front ().contributing_area_m2) >
      1e-4f;
    if (reach.alignment.points.size () == reference.alignment.points.size ())
      for (std::size_t point = 1; point + 1 < reach.alignment.points.size ();
           ++point)
        interior_bends |=
          std::fabs (reach.alignment.points[point].z_m -
                     reference.alignment.points[point].z_m) > 1e-3f;
  }
  MOPPE_CHECK (area_differs);
  MOPPE_CHECK (interior_bends);
}

MOPPE_TEST (waterfall_selection_clusters_adjacent_steep_steps) {
  constexpr std::size_t count = 12;
  const TerrainDomain grid (
    6, 2, 2.0f * mp_units::si::metre, 2.0f * mp_units::si::metre);
  const std::vector<CellIndex> receiver {
    1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11
  };
  const std::vector<float> levels (count, 0.0f);
  const std::vector<float> depths (count, 0.0f);
  const std::array<float, count> slopes { 0.1f, 0.6f, 1.2f, 0.1f, 0.8f, 0.0f,
                                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  const std::array<float, count> areas { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f,
                                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
  const FloodField flood { .surface = make_flood_surface (grid, levels, depths),
                           .sea_level = 0.0f,
                           .has_ocean = false,
                           .ocean = std::vector<std::uint8_t> (count, 0),
                           .spill_receiver = receiver };
  const LakeCensus census (std::vector<WaterBodyId> (count, LakeCensus::dry),
                           {});
  const DrainageGraph drainage { .readings =
                                   make_drainage_readings (grid, slopes, areas),
                                 .receiver = receiver };

  const RiverNetwork rivers = extract_river_network (
    flood,
    census,
    drainage,
    1.0f * mp_units::si::metre * mp_units::si::metre,
    { .minimum_drop = 1.0f * mp_units::si::metre,
      .minimum_slope = 0.5f * terrain_slope[mp_units::one],
      .separation_cells = separation_cell_count (1) });

  MOPPE_CHECK (rivers.reaches.size () == 1);
  MOPPE_CHECK (rivers.waterfalls.size () == 2);
  MOPPE_CHECK (rivers.waterfalls[0].lip_cell == 2);
  MOPPE_CHECK (rivers.waterfalls[0].foot_cell == 3);
  MOPPE_CHECK_NEAR (
    (rivers.waterfalls[0].drop).numerical_value_in (moppe::u::m), 2.4f, 1e-6f);
  MOPPE_CHECK (rivers.waterfalls[1].lip_cell == 4);
}
