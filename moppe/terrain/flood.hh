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
    // Largest distance from any member cell to the shore: half the flooded
    // width at the body's widest point. Size classes cannot tell a flooded
    // channel segment from a lake; this shape reading can.
    meters_t inradius;
    // A permanent body narrow enough for the river ribbon to own: flooded
    // channel water rather than a standing lake. The sheet renderer's
    // one-flat-level-per-body model terraces such bodies into plates, while
    // a ribbon renders them as pools of the river that flows through them.
    bool channel_like;
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

  // The one shared reading of "this body is real water, not a transient
  // puddle". The census, the standing-water sheet, and river routing must
  // agree on it or rivers dry up at bodies the sheet never renders.
  bool water_body_is_permanent (const WaterBody& body,
                                const WaterPermanence& permanence = {});

  // Rivers terminate only at bodies wide enough to stand as sheets: the sea
  // and permanent non-channel lakes. Channel-like bodies pass rivers through
  // and become ribbon-rendered pools of the river that traverses them.
  bool water_body_terminates_rivers (const WaterBody& body,
                                     const WaterPermanence& permanence = {});

  FloodField analyze_standing_water (const TerrainView& terrain,
                                     float sea_level);
  LakeCensus census_lakes (const FloodField& flood, float wet_epsilon = 1e-7f);
  ScalarRaster permanent_water_surface (const FloodField& flood,
                                        const LakeCensus& census,
                                        const WaterPermanence& permanence = {});
}

#endif
