#ifndef MOPPE_TERRAIN_MOISTURE_HH
#define MOPPE_TERRAIN_MOISTURE_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/terrain_view.hh>

namespace moppe::terrain {
  // How wet the ground is for vegetation and material rendering, in
  // [0, 1]: proximity to standing water dominates, with a smaller term
  // from accumulated drainage so runnels read damp between the lakes.
  struct MoistureParameters {
    float water_reach_m = 45.0f;
    float drainage_weight = 0.25f;
    float drainage_span_log2 = 14.0f;
  };

  ScalarRaster analyze_moisture
    (const FloodField& flood, const LakeCensus& census,
     const DrainageGraph& drainage,
     const MoistureParameters& parameters = { });
}

#endif
