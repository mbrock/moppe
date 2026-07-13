#ifndef MOPPE_TERRAIN_WATERCOURSE_HH
#define MOPPE_TERRAIN_WATERCOURSE_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/terrain_view.hh>

#include <vector>

namespace moppe::terrain {
  // Water as a property of the ground. Rivers are not separate ribbon
  // objects laid over the terrain: they are painted into the same
  // per-cell sheets the lakes and the sea already render from — how
  // high the water stands, how it swells, and which way it moves.
  struct WatercoursePaint {
    // Fraction of the carved channel depth the river surface fills;
    // the rest stays visible as bank freeboard.  Matches the fill the
    // ribbon renderer used, so painted rivers ride the same beds.
    float channel_fill = 0.75f;
    // The painted surface never stands more than this many meters over
    // the ground beneath it: down a cascade the sheet hugs the falling
    // bed instead of bridging the drop with a horizontal ledge.
    meters_t depth_limit = 2.0f * mp_units::si::metre;
    // Painted level reaches this far past the channel half width so
    // the waterline lands on the bank ramp and feathers there.
    meters_t bank_margin = 3.0f * mp_units::si::metre;
    // The fill is bounded by the channel actually carved under the
    // reach: each side's bank is the highest ground along a short
    // perpendicular ray ending this far beyond the half width. Where
    // backwater floors kept the carve shallow — the flats behind a
    // mouth — the law's full depth would flood the plain.
    meters_t bank_probe = 7.0f * mp_units::si::metre;
    // But a visible reach never runs dry: the thinnest painted sheet,
    // deep enough to read as water crossing a wide flat basin.
    meters_t minimum_depth = 0.35f * mp_units::si::metre;
    // Flow speed law: base drift plus rapid and waterfall terms, the
    // same advection speeds the ribbon shader scrolled its noise at.
    meters_per_second_t base_speed =
      2.0f * mp_units::si::metre / mp_units::si::second;
    meters_per_second_t rapid_speed =
      3.5f * mp_units::si::metre / mp_units::si::second;
    meters_per_second_t waterfall_speed =
      5.0f * mp_units::si::metre / mp_units::si::second;
    WaterPermanence permanence = {};
  };

  struct WaterSheets {
    // Normalized surface level per unique cell.  Dry cells hold the
    // ground height -- except beside water, where they hold the
    // neighboring body's level (kept a hair below ground): the
    // bilinear water-minus-ground difference then crosses zero at the
    // true sub-cell waterline instead of quantizing onto the lattice.
    ScalarRaster surface;
    // Wave amplitude factor per unique cell: the sea keeps its full
    // swell, lakes barely stir, rivers carry no swell at all — their
    // motion lives in the flow sheet instead.
    std::vector<float> amplitude;
    // Interleaved (x, z) flow in meters per second per unique cell;
    // zero where the water stands still.  River stamps overlapping at
    // a confluence average by weight, which is the whole junction
    // treatment: two currents blending into one.
    std::vector<float> flow;
  };

  WaterSheets paint_watercourses (const TerrainView& terrain,
                                  const FloodField& flood,
                                  const LakeCensus& census,
                                  const DrainageGraph& drainage,
                                  const RiverNetwork& rivers,
                                  const WatercoursePaint& parameters = {});
}

#endif
