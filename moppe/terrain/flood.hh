#ifndef MOPPE_TERRAIN_FLOOD_HH
#define MOPPE_TERRAIN_FLOOD_HH

#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/terrain_view.hh>

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
    std::vector<std::uint32_t> spill_receiver;
    std::vector<std::uint32_t> outlets;

    std::size_t width () const noexcept {
      return source_grid.unique_width ();
    }

    std::size_t height () const noexcept {
      return source_grid.unique_height ();
    }
  };

  enum class WaterBodyClass {
    Puddle,
    Pond,
    Lake,
    Sea
  };

  struct WaterBody {
    static constexpr std::uint32_t no_cell =
      std::numeric_limits<std::uint32_t>::max ();

    std::uint32_t id;
    std::size_t cells;
    float area_m2;
    float maximum_depth_m;
    float mean_depth_m;
    float volume_m3;
    float surface_level_m;
    bool ocean_connected;
    std::uint32_t outlet_cell;
    std::uint32_t spill_cell;
    WaterBodyClass classification;
  };

  struct LakeCensus {
    static constexpr std::uint32_t dry =
      std::numeric_limits<std::uint32_t>::max ();

    std::vector<std::uint32_t> body;
    std::vector<WaterBody> bodies;
  };

  struct WaterPermanence {
    float minimum_area_m2 = 600.0f;
    float minimum_depth_m = 0.25f;
    float minimum_volume_m3 = 100.0f;
  };

  FloodField analyze_standing_water
    (const TerrainView& terrain, float sea_level);
  LakeCensus census_lakes
    (const FloodField& flood, float wet_epsilon = 1e-7f);
  ScalarRaster permanent_water_surface
    (const FloodField& flood, const LakeCensus& census,
     const WaterPermanence& permanence = { });
}

#endif
