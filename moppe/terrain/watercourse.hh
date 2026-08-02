#ifndef MOPPE_TERRAIN_WATERCOURSE_HH
#define MOPPE_TERRAIN_WATERCOURSE_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <vector>

namespace moppe::terrain {
  // Every horizontal water surface is one field over the terrain lattice.
  // Lakes and the sea contribute their still levels; continuous river
  // alignments paint shallow moving levels and flow vectors into the same
  // field. The renderer clips its mesh to the field's exact bilinear
  // waterline, so bends and confluences are unions rather than overlapping
  // reach-owned surfaces. Vertical nickpoint curtains remain explicit geometry.
  struct WatercoursePaint {
    float channel_fill = 0.72f;
    // A running surface may follow a steep bed but never bridge a drop by
    // standing arbitrarily high above the ground below it.
    meters_t depth_limit = 2.5f * mp_units::si::metre;
    // The level probe reaches just onto the bank ramp; terrain clips the
    // resulting field back to the physical waterline.
    meters_t bank_margin = 3.0f * mp_units::si::metre;
    // Flow speed law shared by field shading and waterfall advection.
    meters_per_second_t base_speed =
      2.0f * mp_units::si::metre / mp_units::si::second;
    meters_per_second_t rapid_speed =
      3.5f * mp_units::si::metre / mp_units::si::second;
    meters_per_second_t waterfall_speed =
      5.0f * mp_units::si::metre / mp_units::si::second;
    WaterPermanence permanence = {};
  };

  // The painted water sheet over the terrain lattice. Dry cells hold ground
  // elevation, except beside water where the neighboring level is kept just
  // below ground so bilinear reconstruction crosses the true waterline.
  // Velocity is zero for still water; overlapping channel currents blend.
  using WaterSheets = spatial::
    Bundle<TerrainDomain, SurfaceElevation, WaveAmplitude, WaterVelocity>;

  namespace detail {
    WaterSheets
    paint_watercourses (const TerrainDomain& domain,
                        std::span<const SurfaceElevation> elevations,
                        const FloodField& flood,
                        const LakeCensus& census,
                        const DrainageGraph& drainage,
                        const RiverNetwork& rivers,
                        const WatercoursePaint& parameters);
  }

  template <TerrainElevations Terrain>
  WaterSheets paint_watercourses (const Terrain& terrain,
                                  const FloodField& flood,
                                  const LakeCensus& census,
                                  const DrainageGraph& drainage,
                                  const RiverNetwork& rivers,
                                  const WatercoursePaint& parameters = {}) {
    return detail::paint_watercourses (terrain.domain (),
                                       elevations (terrain),
                                       flood,
                                       census,
                                       drainage,
                                       rivers,
                                       parameters);
  }
}

#endif
