#ifndef MOPPE_TERRAIN_MOISTURE_HH
#define MOPPE_TERRAIN_MOISTURE_HH

#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

namespace moppe::terrain {
  using MoistureMap = spatial::Bundle<TerrainDomain, SurfaceMoisture>;

  // How wet the ground is for material rendering, in
  // [0, 1]: proximity to standing water dominates, with a smaller term
  // from accumulated drainage so runnels read damp between the lakes.
  struct MoistureParameters {
    float water_reach_m = 45.0f;
    float drainage_weight = 0.25f;
    float drainage_span_log2 = 14.0f;
  };

  MoistureMap analyze_moisture (const FloodField& flood,
                                const WaterBodyMembership& water_bodies,
                                const DrainageGraph& drainage,
                                const MoistureParameters& parameters = {});
}

#endif
