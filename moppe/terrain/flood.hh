#ifndef MOPPE_TERRAIN_FLOOD_HH
#define MOPPE_TERRAIN_FLOOD_HH

#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/terrain_view.hh>

#include <cstdint>
#include <vector>

namespace moppe::terrain {
  // The standing liquid surface over a materialized terrain. Rasters contain
  // unique torus samples; spill_receiver forms a deterministic forest leading
  // from every cell to a sea-level seed (or the global-minimum fallback).
  struct FloodField {
    TerrainGrid source_grid;
    float sea_level;
    ScalarRaster water_level;
    ScalarRaster water_depth;
    std::vector<std::uint32_t> spill_receiver;
    std::vector<std::uint32_t> outlets;

    std::size_t width () const noexcept {
      return source_grid.unique_width ();
    }

    std::size_t height () const noexcept {
      return source_grid.unique_height ();
    }
  };

  FloodField analyze_standing_water
    (const TerrainView& terrain, float sea_level);
}

#endif
