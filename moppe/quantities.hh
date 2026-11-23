#ifndef MOPPE_QUANTITIES_HH
#define MOPPE_QUANTITIES_HH

// Only the parts of the ISQ and the SI the game speaks: the whole of
// <mp-units/systems/isq.h>, <si.h>, and <astronomy.h> would cost every
// translation unit that includes this file several seconds of parsing.
#include <mp-units/framework.h>
#include <mp-units/systems/angular/units.h>
#include <mp-units/systems/isq/light_and_radiation.h>
#include <mp-units/systems/isq/mechanics.h>
#include <mp-units/systems/isq/space_and_time.h>
#include <mp-units/systems/si/core.h>

// Every quantity specification in the game, in one row. A spec says what a
// number means; the concrete quantity and quantity_point aliases built on one
// stay with the code that owns the concept, since those need the units,
// vectors, and lattices this file deliberately knows nothing about. Reading
// top to bottom should show what the world is made of and which meanings are
// deliberately kept apart.
//
// Declare with QUANTITY_SPEC. mp-units picks between a CRTP and a
// deducing-this formulation depending on the toolchain, and the macro writes
// whichever one the build needs; spelling the struct out by hand silently
// requires the deducing-this branch.

namespace moppe {
  using mp_units::non_negative;
  using mp_units::quantity_spec;

  inline constexpr auto dimensionless = mp_units::dimensionless;

  // ---- Measured units ----

  using meters_t = mp_units::quantity<mp_units::si::metre, float>;
  using meters_f64_t = mp_units::quantity<mp_units::si::metre, double>;
  // Seconds kept in double, for clocks that run for a whole session.
  using seconds_f64_t = mp_units::quantity<mp_units::si::second, double>;
  using square_meters_t =
    mp_units::quantity<mp_units::si::metre * mp_units::si::metre, float>;
  using cubic_meters_t =
    mp_units::quantity<mp_units::si::metre * mp_units::si::metre *
                         mp_units::si::metre,
                       float>;
  using cubic_meters_f64_t =
    mp_units::quantity<mp_units::si::metre * mp_units::si::metre *
                         mp_units::si::metre,
                       double>;
  using meters_per_second_t =
    mp_units::quantity<mp_units::si::metre / mp_units::si::second, float>;
  // mp-units' astronomy::Julian_year, spelled here so that the world's ages
  // need neither <mp-units/systems/astronomy.h> nor the IAU and CODATA
  // constants it brings.
  namespace astronomy {
    inline constexpr struct Julian_year final
        : mp_units::named_unit<"a",
                               mp_units::mag_ratio<36'525, 100> *
                                 mp_units::non_si::day> {
    } Julian_year;
  }

  using julian_years_t = mp_units::quantity<astronomy::Julian_year, float>;
  using julian_years_f64_t = mp_units::quantity<astronomy::Julian_year, double>;
  using meters_per_julian_year_t =
    mp_units::quantity<mp_units::si::metre / astronomy::Julian_year, float>;
  using square_meters_per_julian_year_t =
    mp_units::quantity<mp_units::si::metre * mp_units::si::metre /
                         astronomy::Julian_year,
                       float>;

  // ---- Signals ----

  // A stick or throttle, and a noise sample: kinds of their own, so a
  // bare float never becomes one and neither becomes a share.
  QUANTITY_SPEC (control_signal, mp_units::dimensionless, mp_units::subkind);
  QUANTITY_SPEC (noise_signal, mp_units::dimensionless, mp_units::subkind);

  // theoretically we could distinguish e.g. single-octave noise signals
  // so that fBm noise is constructed from such signals, which would be
  // a nice example of how to get semantic type safety from dimensionless
  // quantity types... so many interesting possibilities...

  // A share of a whole, and a chance: kinds of their own too.
  QUANTITY_SPEC (proportion,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  QUANTITY_SPEC (probability,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);

  // ---- Kinds of one ----
  //
  // A number without a unit still has a meaning, and two meanings must not
  // be added because their dimensions agree. Each of these is its own kind
  // (subkind): a grade never becomes a blend weight or a scale without an
  // explicit quantity_cast, and a plain `x * mp_units::one` has to be named
  // (`x * grade[one]`) before it becomes one. Coefficients the ISQ already
  // defines are used from there rather than declared again:
  // isq::drag_coefficient, isq::static_friction_coefficient,
  // isq::kinetic_friction_factor, isq::rolling_resistance_factor,
  // isq::mechanical_efficiency, isq::rotation (turns counted), and
  // isq::phase_angle. Angles are already kinds of their own: radians are
  // isq::angular_measure, never plain one (docs/typed-quantities.md).

  // Rise over run along a path: a road's or a trail's climb, a channel's
  // fall. Signed, unlike terrain::terrain_slope, the ground's steepness.
  QUANTITY_SPEC (grade,
                 mp_units::dimensionless,
                 mp_units::isq::height / mp_units::isq::length,
                 mp_units::subkind);
  // How far a blend has gone from one state toward another: a lerp's
  // weight, a smoothing step, a crossfade. Not a share of anything.
  QUANTITY_SPEC (blend_weight,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // How done food is over the fire: none raw, one cooked through (fried
  // golden brown), two burnt black. A stage, not a share: it passes one.
  QUANTITY_SPEC (doneness,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // A size or a strength relative to a reference: a mesh's scale, a gain,
  // a multiplier on a tuned value. May exceed one.
  QUANTITY_SPEC (scale_factor,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // Flow speed over the speed of a shallow-water wave: below one a river
  // is tranquil, above it rapid.
  QUANTITY_SPEC (froude_number,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // A spring's damping over the critical damping that would just stop it
  // overshooting: below one it rings, above one it creeps back.
  QUANTITY_SPEC (damping_ratio,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // Where across a width, signed: -1 at one edge, 0 on the centre line, +1
  // at the other (a waterfall curtain's columns).
  QUANTITY_SPEC (span_coordinate, mp_units::dimensionless, mp_units::subkind);
  // A bed's shear stress over its grains' submerged weight; past its
  // critical value the flow rolls them along.
  QUANTITY_SPEC (shields_number,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // The power one measure grows by with another, d ∝ w^k: how a river's
  // depth follows its width. Not a scale: it is an exponent, raised to.
  QUANTITY_SPEC (scaling_exponent, mp_units::dimensionless, mp_units::subkind);
  // A picture's width over its height: a camera's frame, a viewport.
  QUANTITY_SPEC (aspect_ratio,
                 mp_units::dimensionless,
                 mp_units::isq::width / mp_units::isq::height,
                 mp_units::non_negative,
                 mp_units::subkind);
  // Which way, and nothing of how far: a unit-length vector. A normal, a
  // forward, an axis; the plan's version is a unit Vec2.
  QUANTITY_SPEC (direction,
                 mp_units::dimensionless,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::subkind);

  // ---- Angles ----
  //
  // Which way something faces round the world's up axis, and how far it
  // leans or looks above the level. Both are angular measures, so either
  // converts to a plain radian quantity, but neither becomes the other.
  QUANTITY_SPEC (heading, mp_units::isq::angular_measure);
  QUANTITY_SPEC (inclination, mp_units::isq::angular_measure);

  // ---- Pointers ----
  //
  // How far a pointer's wheel was turned, in its steps or the fraction of
  // one a touchpad gives.
  QUANTITY_SPEC (wheel_travel, mp_units::dimensionless, mp_units::subkind);

  // ---- History ----

  // How long ago something happened, or how long it has stood: a road's
  // founding, a stand's last fire. Counted in Julian years.
  QUANTITY_SPEC (age, mp_units::isq::duration);

  // ---- Soaring ----
  //
  // Soaring quantities are kept distinct even where their dimensions match
  // more general motion quantities.  A vertical air-mass velocity and an
  // aircraft's horizontal speed are both length/time, but adding or comparing
  // them accidentally is a domain error rather than useful arithmetic.

  QUANTITY_SPEC (rate_of_climb_speed,
                 mp_units::isq::speed,
                 mp_units::isq::height / mp_units::isq::duration);
  QUANTITY_SPEC (airspeed, mp_units::isq::speed, mp_units::subkind);
  QUANTITY_SPEC (glide_ratio,
                 mp_units::dimensionless,
                 airspeed / rate_of_climb_speed,
                 mp_units::non_negative);

  // ---- Modeled space ----

  // The dimensions of a modeled spatial domain.  It has the vector
  // character and length dimension of ISQ displacement, but is not a
  // displacement undergone by an object.
  QUANTITY_SPEC (spatial_extent, mp_units::isq::displacement);

  // A displacement in the horizontal plane, and its two axes. Each axis is
  // its own kind (subkind), so an x offset and a z offset never add or
  // compare, and building a plan displacement from them in the wrong order
  // does not compile: `auto [x, z] = d;` and `{ x_displacement (..),
  // z_displacement (..) }` (moppe/gfx/math.hh, plan_displacement_t).
  QUANTITY_SPEC (horizontal_displacement, mp_units::isq::displacement);
  QUANTITY_SPEC (x_displacement,
                 mp_units::isq::displacement,
                 mp_units::subkind);
  QUANTITY_SPEC (z_displacement,
                 mp_units::isq::displacement,
                 mp_units::subkind);

  // ---- The screen ----
  //
  // Lengths on the display in its logical points, which the HUD and the
  // map sheet lay out in, a pointer and the safe insets are measured in,
  // and the host maps to pixels. They have their own
  // dimension: a point on the glass is no length in the world and never
  // converts to a metre.
  inline constexpr struct dim_display_length final
      : mp_units::base_dimension<"P"> {
  } dim_display_length;
  QUANTITY_SPEC (display_length, dim_display_length);
  inline constexpr struct display_point final
      : mp_units::named_unit<"pt", mp_units::kind_of<display_length>> {
  } display_point;
  // Lengths in a raster's own pixels: an image being drawn or read, such
  // as the map sheet's line work. Another dimension again, since how many
  // pixels a point covers is the host's render scale at run time, a
  // quantity in px/pt, never a fixed conversion.
  inline constexpr struct dim_raster_length final
      : mp_units::base_dimension<"Px"> {
  } dim_raster_length;
  QUANTITY_SPEC (raster_length, dim_raster_length);
  inline constexpr struct pixel final
      : mp_units::named_unit<"px", mp_units::kind_of<raster_length>> {
  } pixel;

  // How solid something drawn is, from unseen (0) to opaque (1): a fade's
  // level, a colour's alpha.
  QUANTITY_SPEC (opacity, proportion);
}

template <>
struct mp_units::component_vectors<moppe::horizontal_displacement>
    : mp_units::vector_axes<moppe::x_displacement, moppe::z_displacement> {};

namespace moppe::terrain {
  // ---- Counted entities ----
  //
  // Numbers of entities are quantities of dimension one.  Distinct kinds
  // preserve their algebra while preventing unrelated counts from mixing.

  QUANTITY_SPEC (cell_count, mp_units::dimensionless, mp_units::subkind);
  QUANTITY_SPEC (reach_count, mp_units::dimensionless, mp_units::subkind);
  QUANTITY_SPEC (iteration_count, mp_units::dimensionless, mp_units::subkind);
  QUANTITY_SPEC (separation_cell_count,
                 mp_units::dimensionless,
                 mp_units::subkind);

  // ---- Ground ----

  // A point in the world's vertical reference frame. Terrain storage uses
  // metres directly; differences between elevations are ordinary lengths.
  QUANTITY_SPEC (surface_elevation, mp_units::isq::height, mp_units::subkind);
  QUANTITY_SPEC (terrain_normal,
                 mp_units::dimensionless,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::subkind);
  QUANTITY_SPEC (terrain_slope,
                 mp_units::dimensionless,
                 mp_units::non_negative);

  // ---- Water ----

  QUANTITY_SPEC (surface_moisture, proportion);
  // How much water the ground itself holds, as against how near it is to
  // water. Upslope catchment says how much arrives; local slope says how
  // fast it leaves again. A valley floor and a gully can gather the same
  // water and be nothing alike to stand on, which surface_moisture cannot
  // say and a plant cares about more than anything else.
  QUANTITY_SPEC (soil_wetness, proportion);
  QUANTITY_SPEC (waterline_distance, mp_units::isq::length, mp_units::subkind);
  QUANTITY_SPEC (standing_water_depth,
                 mp_units::isq::length,
                 mp_units::non_negative);
  QUANTITY_SPEC (wave_amplitude, proportion);
  QUANTITY_SPEC (water_velocity,
                 mp_units::isq::speed,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::subkind);
  // How fine the bed a river's flood leaves, on the logarithmic scale grain
  // sizes are graded on: 0 for cobbles and coarser, 1 for sand. Negative
  // where no river sorted the ground.
  QUANTITY_SPEC (bed_grain_fineness,
                 mp_units::dimensionless,
                 mp_units::subkind);
  // How far up a river's strand the ground stands, as a share of the rise
  // from the water's edge (0) to the spate's line (1). Negative off the
  // strands.
  QUANTITY_SPEC (strand_height, mp_units::dimensionless, mp_units::subkind);

  // ---- Drainage ----

  QUANTITY_SPEC (contributing_area,
                 mp_units::isq::area,
                 mp_units::non_negative);
  QUANTITY_SPEC (flow_fraction,
                 mp_units::dimensionless,
                 mp_units::non_negative);
  QUANTITY_SPEC (facet_coordinate,
                 mp_units::dimensionless,
                 mp_units::non_negative);
  QUANTITY_SPEC (drainage_direction,
                 mp_units::angular::angle,
                 mp_units::subkind);
  QUANTITY_SPEC (fractional_contributing_area,
                 mp_units::isq::area,
                 mp_units::non_negative);
  // A ratio's base-2 logarithm, counted in doublings: how many octaves of
  // catchment a cell gathers, or how far a wetness index runs.
  QUANTITY_SPEC (octave_span, mp_units::dimensionless, mp_units::subkind);
  // The power catchment area is raised to in a stream-power law: how much
  // faster a bigger river cuts.
  QUANTITY_SPEC (stream_power_exponent,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);

  // A horizontal, unit-length representative of the directed flow carried by
  // a lattice sample. Unlike drainage_direction, this can be averaged across
  // incoming arcs without an angular wrap discontinuity.
  QUANTITY_SPEC (channel_tangent,
                 mp_units::dimensionless,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::subkind);

  // Contributing area carried in the channel-tangent direction. The arcs in
  // FractionalFlowDomain are the edge-like transport object; this cell-aligned
  // vector is their continuous summary for accumulation and later sampling.
  QUANTITY_SPEC (channel_area_flux,
                 mp_units::isq::area,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::subkind);
  QUANTITY_SPEC (channel_persistence,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);

  // ---- Geology ----

  QUANTITY_SPEC (continent_shape, mp_units::dimensionless);
  QUANTITY_SPEC (uplift_weight, mp_units::dimensionless);
  // Solid material carried, stored, or exported by one geological step.
  // This is deliberately not water volume: equal dimensions do not make the
  // two reservoirs interchangeable.
  QUANTITY_SPEC (sediment_volume,
                 mp_units::isq::volume,
                 mp_units::non_negative);
  QUANTITY_SPEC (sediment_thickness,
                 mp_units::isq::height,
                 mp_units::non_negative);
  // Solid volume carried per water volume at unit channel slope. This is a
  // transport calibration, not a generic proportion or a water-flow share.
  QUANTITY_SPEC (sediment_concentration,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // Local bedrock erodibility relative to the stream-power reference rate:
  // below one for hard granite and limestone caprock, above one for soft
  // shale interbeds and fractured fissure corridors.
  QUANTITY_SPEC (erodibility_factor,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // How strongly a site lies within a structural bedrock fissure corridor.
  QUANTITY_SPEC (fissure_intensity, proportion);

  // ---- Human use ----

  QUANTITY_SPEC (trail_influence, proportion);
  QUANTITY_SPEC (home_base_influence, proportion);
  // How much a planner likes one candidate over another: a weighted sum of
  // tuned preferences, compared only with scores from the same planner.
  QUANTITY_SPEC (planning_score, mp_units::dimensionless, mp_units::subkind);
}

namespace moppe::map {
  // ---- Ground material ----

  // The upward component of a broad local support plane. Snow responds to
  // this material-scale reading rather than the detailed lighting normal.
  QUANTITY_SPEC (snow_support, proportion);

  // Metres of surface the world's history has taken from or laid onto a
  // column. Children of the elevation kind: an elevation change enters one
  // of these by an explicit narrowing, never by dropping to a bare number.
  QUANTITY_SPEC (eroded_surface_material, terrain::surface_elevation);
  QUANTITY_SPEC (deposited_surface_material, terrain::surface_elevation);

  // ---- Analysed readings ----

  // Normalized exposure of material removed during the world's history.
  QUANTITY_SPEC (erosion_exposure, proportion);

  // Normalized cover of material deposited during the world's history.
  QUANTITY_SPEC (deposition_cover, proportion);

  // Ecological support from drainage moisture, slope, shore, and tree line.
  QUANTITY_SPEC (tree_habitat, proportion);

  // Actual canopy recruitment after habitat, routes, and settlement.
  QUANTITY_SPEC (forest_cover, proportion);

  // ---- Bedrock and vegetation ----

  // How much of the ground stands on the limestone laid over the crystalline
  // basement, rather than on the basement erosion has cut down to.
  QUANTITY_SPEC (limestone_cover, proportion);
  // How much of the ground is the basement itself, scoured bare by the ice:
  // granite whalebacks on knolls and ridges where the soil never stayed.
  QUANTITY_SPEC (bedrock_exposure, proportion);

  // How much of a place each species holds, after its tolerance of the site
  // and its competition with the others there. The trees' shares of the
  // canopy...
  QUANTITY_SPEC (spruce_abundance, proportion);
  QUANTITY_SPEC (pine_abundance, proportion);
  QUANTITY_SPEC (birch_abundance, proportion);
  // ...the fell birch of the treeline, low and many-stemmed...
  QUANTITY_SPEC (mountain_birch_abundance, proportion);
  // ...and the field layer beneath: reindeer lichen; heather and crowberry;
  // bilberry; sphagnum; dwarf birch; sedge; and the alvar's short turf on
  // bare limestone. Grass and herbs are what the ground carries elsewhere.
  QUANTITY_SPEC (lichen_abundance, proportion);
  QUANTITY_SPEC (heather_abundance, proportion);
  QUANTITY_SPEC (bilberry_abundance, proportion);
  QUANTITY_SPEC (sphagnum_abundance, proportion);
  QUANTITY_SPEC (dwarf_birch_abundance, proportion);
  QUANTITY_SPEC (sedge_abundance, proportion);
  QUANTITY_SPEC (alvar_abundance, proportion);
  // ...and fireweed, the first to take ground that something has opened.
  QUANTITY_SPEC (fireweed_abundance, proportion);
  // ...and the spring fen: brown moss, sedge, and marsh marigold where the
  // water that sank through the limestone comes out at its foot.
  QUANTITY_SPEC (spring_fen_abundance, proportion);

  // ---- Disturbance ----

  // What has happened to the ground, as the vegetation reads it. Grazing
  // is how hard the beasts of a place still lived in crop it; recovery how
  // far the ground has come back since it was last opened -- burnt,
  // cleared, kept as pasture, or cut for a road -- from nothing to the old
  // stand it would be had nothing happened; burn how recently fire went
  // over it, which fades from black in a decade.
  QUANTITY_SPEC (grazing_pressure, proportion);
  QUANTITY_SPEC (stand_recovery, proportion);
  QUANTITY_SPEC (burn_recency, proportion);
}

namespace moppe::game {
  // ---- Weather ----

  // The covered fraction of the sky. It remains distinct from terrain and
  // vegetation proportions until the renderer narrows it to a GPU lane.
  QUANTITY_SPEC (cloud_cover, proportion);

  // ---- The rider ----

  // How much harm the rider can still take, counted in points: a hundred
  // when whole, none when a life is lost. A crash may take it below
  // nothing for a moment, so it is not a proportion of anything.
  QUANTITY_SPEC (health, mp_units::dimensionless, mp_units::subkind);

  // ---- Fishing ----

  // How far a fish on the line twists to one side (positive) or the
  // other, as a share of its fullest bend: signed, so not a proportion.
  QUANTITY_SPEC (wriggle, mp_units::dimensionless, mp_units::subkind);

  // ---- Organisms ----

  // A tree's size relative to the species' mature representative. Unlike a
  // proportion this may exceed one, and unlike a bare scalar it cannot be
  // confused with canopy recruitment or a random draw.
  QUANTITY_SPEC (tree_size_factor,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);
  // Leaf area over the ground area it covers: the canopy's, or the fallen
  // leaves' on the forest floor.
  QUANTITY_SPEC (leaf_area_index,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::subkind);

  // How a beast carries its head: 0 level, 1 down to the grass, and below
  // zero raised to look about (game/animals.hh).
  QUANTITY_SPEC (head_carriage, mp_units::dimensionless);
}

#endif
