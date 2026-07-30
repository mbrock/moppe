#ifndef MOPPE_TERRAIN_DRAINAGE_HH
#define MOPPE_TERRAIN_DRAINAGE_HH

#include <moppe/terrain/domain.hh>

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
    std::vector<float> slope;
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
  // Distance is local to the reach, while flow_distance_m is globally
  // continuous through confluences and reaches zero at the final mouth.
  struct RiverAlignmentPoint {
    float x_m = 0.0f;
    float z_m = 0.0f;
    float distance_m = 0.0f;
    float flow_distance_m = 0.0f;
    float contributing_area_m2 = 0.0f;
    float slope = 0.0f;
    float waterfall = 0.0f;
    float standing_water = 0.0f;
    float water_level_m = 0.0f;
    // Flooded water the ribbon itself owns: channel-like bodies and small
    // depressions the route crosses. The ribbon holds water_level_m across
    // such stretches and widens to the local waterline, where standing_water
    // instead retires the ribbon under a sheet-rendered body.
    float pooled = 0.0f;
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
    // inlet to outlet. Traversed channel-like bodies render as ribbon pools
    // and must therefore stay out of the standing-water sheet.
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
