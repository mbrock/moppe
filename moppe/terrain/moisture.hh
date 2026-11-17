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
  using OctaveSpan = quantity<octave_span[one], float>;

  struct MoistureParameters {
    meters_t water_reach = 45.0f * u::m;
    blend_weight_t drainage_weight = 0.25f * blend_weight[one];
    OctaveSpan drainage_span = 14.0f * octave_span[one];

    // Soil wetness is the topographic wetness index: catchment says how much
    // water arrives, slope says how fast it leaves again. Both terms are
    // logarithms, so the index is a difference of octaves and the span below
    // is how many of them separate a bare ridge from a saturated flat.
    //
    // The floor is the flattest slope the index will believe. Without one a
    // level cell divides by nothing, and a lake bed would read as infinitely
    // wet -- which is true, and useless.
    OctaveSpan wetness_span = 16.0f * octave_span[one];
    slope_t flattest_believable_slope = 0.002f * terrain_slope[one];
  };

  MoistureMap analyze_moisture (const FloodField& flood,
                                const WaterBodyMembership& water_bodies,
                                const DrainageGraph& drainage,
                                const MoistureParameters& parameters = {});
}

#endif
