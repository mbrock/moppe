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

  // Channel width from catchment area, shared by carving and rendering
  // so carved beds match the ribbons drawn over them.
  float channel_width_m (float area_m2) noexcept;

  ChannelCarvingResult carve_channels
    (const TerrainView& terrain, const ChannelCarving& parameters);
}

#endif
