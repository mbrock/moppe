#ifndef MOPPE_TERRAIN_ANALYTICAL_EROSION_HH
#define MOPPE_TERRAIN_ANALYTICAL_EROSION_HH

#include <moppe/terrain/erosion.hh>
#include <moppe/terrain/terrain_view.hh>

#include <vector>

namespace moppe::terrain {
  struct AnalyticalErosionResult {
    // Unique samples: periodic duplicated render seams are omitted.
    std::vector<float> heights;
    AnalyticalErosionReport report;
  };

  AnalyticalErosionResult erode_analytically
    (const TerrainView& terrain, const AnalyticalErosion& parameters);
}

#endif
