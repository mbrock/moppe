#ifndef MOPPE_TERRAIN_WATERCOURSE_HH
#define MOPPE_TERRAIN_WATERCOURSE_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <vector>

namespace moppe::terrain {
  // Every horizontal water surface is one field over the terrain lattice.
  // Lakes and the sea contribute their still levels; continuous river
  // alignments paint moving levels and flow vectors into the same field.
  // The renderer clips its mesh to the field's exact bilinear waterline, so
  // bends and confluences are unions rather than overlapping reach-owned
  // surfaces. Vertical nickpoint curtains remain explicit geometry.
  //
  // Running water stands where its discharge puts it. Each alignment point
  // reads the valley's cross-section from the terrain, and a river's surface
  // is the steady profile of its discharge (river.hh) through those
  // sections, found from the mouth upward: over a sill or down a steep bed
  // the water runs at the critical pace, which is the least stage that
  // passes it, and above that it stands no lower than the water below it
  // plus the head the bed's friction takes on the way. So a brook, which
  // clears its sills by a hand's breadth, is a stair of pools and spills,
  // and a river with eighty times the water drowns the same sills and runs
  // wide over them on one grade.
  //
  // A cell takes the stage of the section it lies in, the nearest of each
  // reach that covers it, and where reaches lie alongside a level between
  // theirs. Still water is a boundary: a river's own surface ends at a
  // lake or the sea as at a bank, brings it a current but never a level,
  // and draws down to it over the last few cells; and still water standing
  // a step above its neighbour, over a rim, spills there.
  //
  // The current follows from the same sections. Along a river the discharge
  // is what it is, so the water is quick where the section is small and
  // slow where it is large, and across one it runs as the root of its
  // depth: u = c sqrt(d), with the pace c the discharge over the section's
  // conveyance (the integral of d^(3/2) across it). Its vertical part is
  // how fast the water descends its own surface, which is the head it
  // sheds and so what whitens it.
  struct WatercoursePaint {
    // Chezy's coefficient for a cobble bed, sqrt(m)/s: u = C sqrt(d S).
    float roughness = 20.0f;
    // The Froude number steep water holds itself to; a stream on a mobile
    // bed steeper than about one in forty runs close to critical.
    float critical_froude = 1.0f;
    // How many regime widths (river.hh) running water may spread to on a
    // floor too flat to hold it; beyond, the stage gives way instead.
    float spread = 1.5f;
    // A running surface may follow a steep bed but never bridge a drop by
    // standing arbitrarily high above the ground below it.
    meters_t depth_limit = 2.5f * mp_units::si::metre;
    // The level probe reaches just onto the bank ramp; terrain clips the
    // resulting field back to the physical waterline.
    meters_t bank_margin = 3.0f * mp_units::si::metre;
    // What a fall adds to the current at its lip and in its plunge pool.
    meters_per_second_t waterfall_speed =
      5.0f * mp_units::si::metre / mp_units::si::second;
    WaterPermanence permanence = {};
  };

  // The painted water sheet over the terrain lattice. Dry cells hold ground
  // elevation, except beside water where the neighboring level is kept just
  // below ground so bilinear reconstruction crosses the true waterline; and
  // where running water steps diagonally between two dry cells, those keep
  // a level just under their own ground, so the stream crosses the corner.
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
