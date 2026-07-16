#ifndef MOPPE_TERRAIN_WATERCOURSE_HH
#define MOPPE_TERRAIN_WATERCOURSE_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/terrain_view.hh>

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
