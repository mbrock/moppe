#ifndef MOPPE_QUANTITIES_HH
#define MOPPE_QUANTITIES_HH

#include <mp-units/framework.h>
#include <mp-units/systems/angular.h>
#include <mp-units/systems/astronomy.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

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
  using julian_years_t =
    mp_units::quantity<mp_units::astronomy::Julian_year, float>;
  using julian_years_f64_t =
    mp_units::quantity<mp_units::astronomy::Julian_year, double>;
  using meters_per_julian_year_t =
    mp_units::quantity<mp_units::si::metre / mp_units::astronomy::Julian_year,
                       float>;
  using square_meters_per_julian_year_t =
    mp_units::quantity<mp_units::si::metre * mp_units::si::metre /
                         mp_units::astronomy::Julian_year,
                       float>;

  // ---- Signals ----

  QUANTITY_SPEC (control_signal, mp_units::dimensionless);
  QUANTITY_SPEC (noise_signal, mp_units::dimensionless);

  // theoretically we could distinguish e.g. single-octave noise signals
  // so that fBm noise is constructed from such signals, which would be
  // a nice example of how to get semantic type safety from dimensionless
  // quantity types... so many interesting possibilities...

  QUANTITY_SPEC (proportion, mp_units::dimensionless, mp_units::non_negative);
  QUANTITY_SPEC (probability, mp_units::dimensionless, mp_units::non_negative);

  // ---- Soaring ----
  //
  // Soaring quantities are kept distinct even where their dimensions match
  // more general motion quantities.  A vertical air-mass velocity and an
  // aircraft's horizontal speed are both length/time, but adding or comparing
  // them accidentally is a domain error rather than useful arithmetic.

  QUANTITY_SPEC (rate_of_climb_speed,
                 mp_units::isq::speed,
                 mp_units::isq::height / mp_units::isq::duration);
  QUANTITY_SPEC (airspeed, mp_units::isq::speed, mp_units::is_kind);
  QUANTITY_SPEC (glide_ratio,
                 mp_units::dimensionless,
                 airspeed / rate_of_climb_speed,
                 mp_units::non_negative);

  // ---- Modeled space ----

  // The dimensions of a modeled spatial domain.  It has the vector
  // character and length dimension of ISQ displacement, but is not a
  // displacement undergone by an object.
  QUANTITY_SPEC (spatial_extent, mp_units::isq::displacement);
}

namespace moppe::terrain {
  // ---- Counted entities ----
  //
  // Numbers of entities are quantities of dimension one.  Distinct kinds
  // preserve their algebra while preventing unrelated counts from mixing.

  QUANTITY_SPEC (cell_count, mp_units::dimensionless, mp_units::is_kind);
  QUANTITY_SPEC (reach_count, mp_units::dimensionless, mp_units::is_kind);
  QUANTITY_SPEC (iteration_count, mp_units::dimensionless, mp_units::is_kind);
  QUANTITY_SPEC (separation_cell_count,
                 mp_units::dimensionless,
                 mp_units::is_kind);

  // ---- Ground ----

  // A point in the world's vertical reference frame. Terrain storage uses
  // metres directly; differences between elevations are ordinary lengths.
  QUANTITY_SPEC (surface_elevation, mp_units::isq::height, mp_units::is_kind);
  QUANTITY_SPEC (terrain_normal,
                 mp_units::dimensionless,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::is_kind);
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
  QUANTITY_SPEC (waterline_distance, mp_units::isq::length, mp_units::is_kind);
  QUANTITY_SPEC (standing_water_depth,
                 mp_units::isq::length,
                 mp_units::non_negative);
  QUANTITY_SPEC (wave_amplitude, proportion);
  QUANTITY_SPEC (water_velocity,
                 mp_units::isq::speed,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::is_kind);

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
                 mp_units::is_kind);
  QUANTITY_SPEC (fractional_contributing_area,
                 mp_units::isq::area,
                 mp_units::non_negative);

  // A horizontal, unit-length representative of the directed flow carried by
  // a lattice sample. Unlike drainage_direction, this can be averaged across
  // incoming arcs without an angular wrap discontinuity.
  QUANTITY_SPEC (channel_tangent,
                 mp_units::dimensionless,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::is_kind);

  // Contributing area carried in the channel-tangent direction. The arcs in
  // FractionalFlowDomain are the edge-like transport object; this cell-aligned
  // vector is their continuous summary for accumulation and later sampling.
  QUANTITY_SPEC (channel_area_flux,
                 mp_units::isq::area,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::is_kind);
  QUANTITY_SPEC (channel_persistence,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::is_kind);

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
                 mp_units::is_kind);

  // ---- Human use ----

  QUANTITY_SPEC (trail_influence, proportion);
  QUANTITY_SPEC (home_base_influence, proportion);
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

  // Planar channel direction scaled by log-compressed fluvial activity.
  QUANTITY_SPEC (channel_flux,
                 mp_units::dimensionless,
                 mp_units::quantity_tensor_order::vector,
                 mp_units::is_kind);

  // Normalized exposure of material removed during the world's history.
  QUANTITY_SPEC (erosion_exposure, proportion);

  // Normalized cover of material deposited during the world's history.
  QUANTITY_SPEC (deposition_cover, proportion);

  // Ecological support from drainage moisture, slope, shore, and tree line.
  QUANTITY_SPEC (tree_habitat, proportion);

  // Actual canopy recruitment after habitat, routes, and settlement.
  QUANTITY_SPEC (forest_cover, proportion);
}

namespace moppe::game {
  // ---- Weather ----

  // The covered fraction of the sky. It remains distinct from terrain and
  // vegetation proportions until the renderer narrows it to a GPU lane.
  QUANTITY_SPEC (cloud_cover, proportion);

  // ---- Organisms ----

  // A tree's size relative to the species' mature representative. Unlike a
  // proportion this may exceed one, and unlike a bare scalar it cannot be
  // confused with canopy recruitment or a random draw.
  QUANTITY_SPEC (tree_size_factor,
                 mp_units::dimensionless,
                 mp_units::non_negative,
                 mp_units::is_kind);
}

#endif
