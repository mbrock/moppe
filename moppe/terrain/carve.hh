#ifndef MOPPE_TERRAIN_CARVE_HH
#define MOPPE_TERRAIN_CARVE_HH

#include <moppe/terrain/erosion.hh>
#include <moppe/terrain/terrain_view.hh>

#include <vector>

namespace moppe::terrain {
  struct ChannelCarvingResult {
    // Unique samples: periodic duplicated render seams are omitted.
    std::vector<float> heights;
    ChannelCarvingReport report;
  };

  // Channel width and carved depth from catchment area, shared by carving
  // and rendering so water surfaces fill the beds carved under them.
  float channel_width_m (float area_m2) noexcept;
  float channel_depth_m (float area_m2,
                         const ChannelCarving& parameters = {}) noexcept;

  // A river exists once the width law reaches two lattice cells: below
  // that the raster cannot hold a channel, and pretending otherwise
  // yields one-cell trenches and wobbling sub-cell water threads.
  // Width grows with the square root of catchment area, so the rule is
  // a resolution-independent cell count; smaller runoff stays with the
  // material and moisture readings instead of rendering as water.
  float river_existence_minimum_area_cells () noexcept;

  ChannelCarvingResult carve_channels (const TerrainView& terrain,
                                       const ChannelCarving& parameters);
}

#endif
