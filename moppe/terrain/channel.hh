#ifndef MOPPE_TERRAIN_CHANNEL_HH
#define MOPPE_TERRAIN_CHANNEL_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <vector>

namespace moppe::terrain {
  // Erosion leaves each river a valley, and along the valley's floor a
  // string of hollows and sills a few samples long: the last strokes of
  // the transport laws, which a river would long since have worked over.
  // This gives each river its bed.
  //
  // First the long profile. Along every watercourse the bed is graded
  // between the two things a river can do with a hollow: fill it to its
  // sill, or cut the sill down to it. Both give a bed that only falls, so
  // any share of each does, and that profile is then eased along the
  // stream. A lake keeps its sill and its level, the sea its shore, and a
  // waterfall its lip; but a pond no bigger than a few widths of the river
  // that runs through it is a pool of that river's, whose sill the river
  // cuts like any other.
  //
  // Then the section. Along each river's smooth alignment the ground is
  // cut, and its hollows filled, to a parabolic bed as wide as the river's
  // discharge makes it (river.hh) and deep in proportion, its deepest
  // below the graded profile and its edges at it, with banks cut back at a
  // slope a walker climbs; or, where the river runs calm, a bar shoaling
  // the bed toward a strand it has planed along its bank, most on the
  // inside of a bend, with the outside cut back steep. Where the course
  // steps diagonally between samples, one of the two beside the step is
  // cut to the bed too, so that read bilinearly the bed runs on from
  // sample to sample rather than through a corner between two banks.
  // Below a lake the bed starts at the sill and deepens downstream, so the
  // lake stands as it did; toward the sea nothing holds it up, and the
  // last of the river lies below sea level.
  struct ChannelFormation {
    // The bed is this share of the regime width, and no narrower than a
    // visible river.
    float width_share = 0.75f;
    meters_t least_width = 5.0f * mp_units::si::metre;
    // Its depth at that least width, growing with the width to this power.
    meters_t least_depth = 0.25f * mp_units::si::metre;
    float depth_growth = 0.8f;
    // How far from leaving a hollow filled (0) to cutting its sill through
    // (1) the graded bed lies.
    float breach = 0.5f;
    // The least a bed falls, so that water always knows its way.
    float least_grade = 0.001f;
    // Passes of easing along the stream: about four metres of reach each.
    int easing = 24;
    // Rise over run of the banks cut back from the bed's edges.
    float bank_slope = 0.4f;
    // Where a river runs calm, under the first of these falls, it lays
    // bars of what it carries along its banks, most on the inside of a
    // bend, and cuts into the outside; above the second it runs steep
    // between whatever banks its valley gives it.
    float calm_grade = 0.005f;
    float steep_grade = 0.03f;
    // A bend is tight, radius over its river's width, under the first and
    // straight over the second.
    float tight_bend = 2.5f;
    float open_bend = 8.0f;
    // A full bar: its slope, a strand to walk along; how far it runs out
    // from the bed, so many of the bed's widths and no less than a few
    // strides; and how much wider it makes the bed on its side.
    float bar_slope = 0.07f;
    float bar_widths = 1.5f;
    meters_t bar_reach = 6.0f * mp_units::si::metre;
    float bar_widening = 0.6f;
    // How much of a full bar a calm river lays along a straight reach,
    // where its bars alternate from one bank to the other.
    float straight_bar = 0.7f;
    // The most a full bar planes the ground beside it down: the river has
    // worked its floor this deep since it last stood higher.
    meters_t bar_lowering = 4.0f * mp_units::si::metre;
    // The outside of a tight calm bend, cut back by the current.
    float cut_bank_slope = 0.8f;
    // A sill is cut no deeper than this; what that leaves of a hollow
    // behind it fills.
    meters_t deepest_cut = 1.2f * mp_units::si::metre;
    // How far the ground may stand over a bed of a given depth and still
    // be its bank; higher is the valley's side, which the bed stops at.
    float bank_height (float depth) const noexcept {
      return 1.5f * depth + 0.25f;
    }
    // A body of standing water is a lake, and keeps its sill, when it
    // covers at least this many squares of the regime width of the river
    // leaving it.
    float lake_widths = 4.0f;
    // A hollow beside the stream deeper than this below the bed is another
    // place, a drop or a side gully, and is not filled.
    meters_t deepest_fill = 1.5f * mp_units::si::metre;
    // Where the course steps diagonally between cells, the lower of the
    // two cells beside the step is cut to the bed, unless it stands more
    // than this over it.
    meters_t deepest_corner = 3.0f * mp_units::si::metre;
    WaterPermanence permanence = {};
  };

  struct ChannelFormationResult {
    std::vector<SurfaceElevation> heights;
    // Metres of ground laid down (positive) or cut away (negative).
    std::vector<float> change_m;
    cubic_meters_f64_t cut =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t filled =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
  };

  namespace detail {
    ChannelFormationResult
    form_channels (const TerrainDomain& domain,
                   std::span<const SurfaceElevation> elevations,
                   const FloodField& flood,
                   const LakeCensus& census,
                   const DrainageGraph& drainage,
                   const RiverNetwork& rivers,
                   const ChannelFormation& parameters);
  }

  template <TerrainElevations Terrain>
  ChannelFormationResult
  form_channels (const Terrain& terrain,
                 const FloodField& flood,
                 const LakeCensus& census,
                 const DrainageGraph& drainage,
                 const RiverNetwork& rivers,
                 const ChannelFormation& parameters = {}) {
    return detail::form_channels (terrain.domain (),
                                  elevations (terrain),
                                  flood,
                                  census,
                                  drainage,
                                  rivers,
                                  parameters);
  }
}

#endif
