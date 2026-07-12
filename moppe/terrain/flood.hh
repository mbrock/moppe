#ifndef MOPPE_TERRAIN_FLOOD_HH
#define MOPPE_TERRAIN_FLOOD_HH

#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/types.hh>

#include <cstdint>
#include <limits>
#include <vector>

namespace moppe::terrain {
  // The standing liquid surface over a materialized terrain. Rasters contain
  // unique torus samples; spill_receiver forms a deterministic forest leading
  // to the largest connected below-sea component (or the global-minimum
  // fallback when the world has no ocean).
  struct FloodField {
    TerrainGrid source_grid;
    float sea_level;
    bool has_ocean;
    ScalarRaster water_level;
    ScalarRaster water_depth;
    std::vector<std::uint8_t> ocean;
    std::vector<CellIndex> spill_receiver;
    std::vector<CellIndex> outlets;

    std::size_t width () const noexcept {
      return source_grid.unique_width ();
    }

    std::size_t height () const noexcept {
      return source_grid.unique_height ();
    }
  };

  enum class WaterBodyClass { Puddle, Pond, Lake, Sea };

  struct WaterBody {
    static constexpr CellIndex no_cell = terrain::no_cell;

    WaterBodyId id;
    CellCount cells;
    square_meters_t area;
    meters_t maximum_depth;
    meters_t mean_depth;
    cubic_meters_t volume;
    meters_t surface_level;
    bool ocean_connected;
    CellIndex outlet_cell;
    CellIndex spill_cell;
    WaterBodyClass classification;
  };

  struct LakeCensus {
    static constexpr WaterBodyId dry = no_water_body;

    std::vector<WaterBodyId> body;
    std::vector<WaterBody> bodies;
  };

  struct WaterPermanence {
    square_meters_t minimum_area =
      600.0f * mp_units::si::metre * mp_units::si::metre;
    meters_t minimum_depth = 0.25f * mp_units::si::metre;
    cubic_meters_t minimum_volume =
      100.0f * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
  };

  FloodField analyze_standing_water (const TerrainView& terrain,
                                     float sea_level);
  LakeCensus census_lakes (const FloodField& flood, float wet_epsilon = 1e-7f);
  ScalarRaster permanent_water_surface (const FloodField& flood,
                                        const LakeCensus& census,
                                        const WaterPermanence& permanence = {});
}

#endif
