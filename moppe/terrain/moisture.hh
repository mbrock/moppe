#ifndef MOPPE_TERRAIN_MOISTURE_HH
#define MOPPE_TERRAIN_MOISTURE_HH

#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

namespace moppe::terrain {
  // Two different questions about water at one place, answered in one pass
  // because they read the same routing. Moisture is how wet the ground looks;
  // wetness is how much water it holds.
  using MoistureMap =
    spatial::Bundle<TerrainDomain, SurfaceMoisture, SoilWetness>;

  // How wet the ground is for material rendering, in
  // [0, 1]: proximity to standing water dominates, with a smaller term
  // from accumulated drainage so runnels read damp between the lakes.
  struct MoistureParameters {
    float water_reach_m = 45.0f;
    float drainage_weight = 0.25f;
    float drainage_span_log2 = 14.0f;

    // Soil wetness is the topographic wetness index: catchment says how much
    // water arrives, slope says how fast it leaves again. Both terms are
    // logarithms, so the index is a difference of octaves and the span below
    // is how many of them separate a bare ridge from a saturated flat.
    //
    // The floor is the flattest slope the index will believe. Without one a
    // level cell divides by nothing, and a lake bed would read as infinitely
    // wet -- which is true, and useless.
    float wetness_span_octaves = 16.0f;
    float flattest_believable_slope = 0.002f;
  };

  MoistureMap analyze_moisture (const FloodField& flood,
                                const WaterBodyMembership& water_bodies,
                                const DrainageGraph& drainage,
                                const MoistureParameters& parameters = {});
}

#endif
