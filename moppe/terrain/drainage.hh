#ifndef MOPPE_TERRAIN_DRAINAGE_HH
#define MOPPE_TERRAIN_DRAINAGE_HH

#include <moppe/terrain/domain.hh>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moppe::terrain {
  using slope_t = mp_units::quantity<terrain_slope[mp_units::one], float>;
  using ContributingArea = quantity<contributing_area[u::m * u::m], float>;
  using DrainageReadings =
    spatial::Bundle<TerrainDomain, slope_t, ContributingArea>;

  inline DrainageReadings
  make_drainage_readings (TerrainDomain domain,
                          std::span<const slope_t> slopes,
                          std::span<const ContributingArea> areas) {
    DrainageReadings readings (std::move (domain));
    if (slopes.size () != readings.size () || areas.size () != readings.size ())
      throw std::invalid_argument (
        "drainage readings do not match terrain domain");
    std::ranges::copy (slopes, spatial::get<terrain_slope> (readings).begin ());
    std::ranges::copy (areas,
                       spatial::get<contributing_area> (readings).begin ());
    return readings;
  }

  // The same from bare numbers: slopes as rise over run, areas in square
  // metres. For tests that write their readings out by hand.
  inline DrainageReadings
  make_drainage_readings (TerrainDomain domain,
                          std::span<const float> slopes,
                          std::span<const float> areas) {
    DrainageReadings readings (std::move (domain));
    if (slopes.size () != readings.size () || areas.size () != readings.size ())
      throw std::invalid_argument (
        "drainage readings do not match terrain domain");
    auto& slope_column = spatial::get<terrain_slope> (readings);
    auto& area_column = spatial::get<contributing_area> (readings);
    for (std::size_t cell = 0; cell < readings.size (); ++cell) {
      slope_column[cell] = slopes[cell] * terrain_slope[mp_units::one];
      area_column[cell] = areas[cell] * contributing_area[u::m * u::m];
    }
    return readings;
  }

  struct FloodField;
  class LakeCensus;

  // One receiver per cell across lakes and proven depression spills. This
  // supplies routes where continuous downhill drainage is undefined and
  // deliberately omits products that fractional drainage computes itself.
  struct WetDrainageRouting {
    TerrainDomain domain;
    std::vector<CellIndex> receiver;
    std::vector<slope_t> slope;
  };

  // A compact functional graph. Dry drainage uses strictly lower receivers;
  // wet drainage may also follow acyclic equal-surface routes through water.
  // Its readings bundle contains the same unique periodic terrain samples.
  struct DrainageGraph {
    DrainageReadings readings;
    std::vector<CellIndex> receiver;

    std::size_t width () const noexcept {
      return readings.domain ().width ();
    }

    std::size_t height () const noexcept {
      return readings.domain ().height ();
    }

    const TerrainDomain& domain () const noexcept {
      return readings.domain ();
    }

    std::span<const slope_t> slopes () const noexcept {
      return spatial::get<terrain_slope> (readings);
    }

    std::span<const ContributingArea> contributing_areas () const noexcept {
      return spatial::get<contributing_area> (readings);
    }

    slope_t slope_at (std::size_t cell) const {
      return slopes ()[cell];
    }

    square_meters_t contributing_area_at (std::size_t cell) const {
      return contributing_areas ()[cell];
    }
  };

  // A dense, unwrapped plan-view trajectory derived from a receiver chain.
  // The drainage cells remain the topological authority; this continuous
  // reading is shared by rendering, flow painting, and future traversal.
  // Distance is local to the reach, while flow_distance is globally
  // continuous through confluences and reaches zero at the final mouth.
  struct RiverAlignmentPoint {
    plan_position_t place = plan_position (0.0f, 0.0f);
    meters_t distance = 0.0f * u::m;
    meters_t flow_distance = 0.0f * u::m;
    square_meters_t contributing_area = 0.0f * u::m2;
    slope_t slope = 0.0f * terrain_slope[one];
    proportion_t waterfall = 0.0f * proportion[one];
    proportion_t standing_water = 0.0f * proportion[one];
    SurfaceElevation water_level = surface_elevation_point (0.0f * u::m);
    // Flooded channel-like bodies and small depressions the route crosses.
    // The running-water painter holds water_level across such stretches;
    // standing_water instead marks a body that terminates the river.
    proportion_t pooled = 0.0f * proportion[one];
  };

  struct RiverAlignment {
    std::vector<RiverAlignmentPoint> points;
    meters_f64_t length = 0.0 * mp_units::si::metre;
  };

  struct RiverReach {
    static constexpr RiverReachId no_id = no_river_reach;

    RiverReachId id;
    std::vector<CellIndex> cells;
    WaterBodyId upstream_body;
    WaterBodyId downstream_body;
    bool downstream_ocean;
    RiverReachId downstream_reach;
    square_meters_t upstream_area;
    square_meters_t downstream_area;
    slope_t maximum_slope;
    RiverAlignment alignment;
  };

  struct WaterfallParameters {
    meters_t minimum_drop = 3.0f * mp_units::si::metre;
    slope_t minimum_slope = 0.6f * terrain_slope[mp_units::one];
    SeparationCellCount separation_cells = separation_cell_count (1);
  };

  struct Waterfall {
    RiverReachId reach_id;
    CellIndex lip_cell;
    CellIndex foot_cell;
    meters_t drop;
    meters_t horizontal_distance;
    slope_t slope;
    square_meters_t contributing_area;
  };

  struct RiverNetwork {
    square_meters_t minimum_area;
    WaterfallParameters waterfall_parameters;
    std::vector<RiverReach> reaches;
    std::vector<Waterfall> waterfalls;
    // One flag per census body: a river alignment passes through it from
    // inlet to outlet. Traversed channel-like bodies remain pooled at their
    // flood level but receive the running field's current and calm amplitude.
    std::vector<std::uint8_t> body_traversed;
  };

  namespace detail {
    DrainageGraph
    analyze_drainage (const TerrainDomain& domain,
                      std::span<const SurfaceElevation> elevations);
  }

  template <TerrainElevations Terrain>
  DrainageGraph analyze_drainage (const Terrain& terrain) {
    return detail::analyze_drainage (terrain.domain (), elevations (terrain));
  }

  WetDrainageRouting route_wet_drainage (const FloodField& flood,
                                         const LakeCensus& census);
  DrainageGraph analyze_wet_drainage (const FloodField& flood,
                                      const LakeCensus& census);
  RiverNetwork
  extract_river_network (const FloodField& flood,
                         const LakeCensus& census,
                         const DrainageGraph& drainage,
                         square_meters_t minimum_area,
                         const WaterfallParameters& waterfall_parameters = {});
}

#endif
