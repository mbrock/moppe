#ifndef MOPPE_TERRAIN_WATERCOURSE_HH
#define MOPPE_TERRAIN_WATERCOURSE_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <vector>

namespace moppe::terrain {
  // Standing water remains a terrain sheet. Running water has explicit
  // continuous geometry; this painter only carries its current through mouths
  // into lakes and the sea so the two representations meet coherently.
  struct WatercoursePaint {
    // Mouth flow reaches slightly past the visible half width so it blends
    // into the slower body current rather than stopping at the shoreline.
    meters_t bank_margin = 3.0f * mp_units::si::metre;
    // Flow speed law shared conceptually with the ribbon shader.
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
  // Velocity is zero for standing water; overlapping mouth currents blend.
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
