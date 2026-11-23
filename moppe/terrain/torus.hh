#ifndef MOPPE_TERRAIN_TORUS_HH
#define MOPPE_TERRAIN_TORUS_HH

#include <moppe/gfx/math.hh>

#include <cmath>
#include <concepts>

// The world is a torus.  These are the wrap operations everything that
// lives on it shares; the period is always the full lattice extent.  A
// player, a vehicle, or a camera is never wrapped back into the world, so
// anything measured against one must be measured by the shortest way
// round (minimum_image_delta), or drawn at the copy of the world nearest
// it (nearest_image).  A period of zero or less means no wrapping.
//
// The float functions are the arithmetic; the typed overloads below them
// apply it lane by lane, so a caller moved onto them computes the same
// bits. The world's period is one plan extent (WorldGround::period ()),
// never a pair of floats.

namespace moppe::terrain {
  inline int wrap_index (int value, int period) {
    const int remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
  }

  inline float wrap_coordinate (float value, float period) {
    const float wrapped = value - std::floor (value / period) * period;
    return wrapped < period ? wrapped : 0.0f;
  }

  // The part of `delta` that is a whole number of laps of the world.
  inline float whole_periods (float delta, float period) {
    return period > 0.0f ? std::round (delta / period) * period : 0.0f;
  }

  // The shortest way across the seam: `delta` brought to within half a
  // period of zero.
  inline float minimum_image_delta (float delta, float period) {
    return delta - whole_periods (delta, period);
  }

  // The lattice's version, in whole cells.
  inline int minimum_image_cells (int delta, int period) {
    if (delta > period / 2)
      delta -= period;
    else if (delta < -period / 2)
      delta += period;
    return delta;
  }

  // `value` at the copy of the world nearest `reference`.
  inline float nearest_image (float value, float reference, float period) {
    return reference + minimum_image_delta (value - reference, period);
  }

  // ---- Typed ----

  inline meters_t wrap_coordinate (meters_t value, meters_t period) {
    return wrap_coordinate (meters_value (value), meters_value (period)) * u::m;
  }

  inline meters_t minimum_image_delta (meters_t delta, meters_t period) {
    return minimum_image_delta (meters_value (delta), meters_value (period)) *
           u::m;
  }

  inline meters_t
  nearest_image (meters_t value, meters_t reference, meters_t period) {
    return nearest_image (meters_value (value),
                          meters_value (reference),
                          meters_value (period)) *
           u::m;
  }

  // A plan position brought into the world's one copy, [0, period).
  inline plan_position_t wrap (const plan_position_t& at,
                               const plan_extent_t& period) {
    const Vec2& p = plan_value (at);
    const Vec2& n = plan_value (period);
    return plan_position (wrap_coordinate (p.x (), n.x ()),
                          wrap_coordinate (p.z (), n.z ()));
  }

  // The shortest way across the seam, lane by lane.
  inline plan_displacement_t minimum_image (const plan_displacement_t& delta,
                                            const plan_extent_t& period) {
    const Vec2& d = plan_value (delta);
    const Vec2& n = plan_value (period);
    return plan_displacement (Vec2 (minimum_image_delta (d.x (), n.x ()),
                                    minimum_image_delta (d.z (), n.z ())));
  }

  // From `from` to `to` by the shortest way round.
  inline plan_displacement_t torus_offset (const plan_position_t& from,
                                           const plan_position_t& to,
                                           const plan_extent_t& period) {
    return minimum_image (to - from, period);
  }

  // `at` at the copy of the world nearest `reference`.
  inline plan_position_t nearest_image (const plan_position_t& at,
                                        const plan_position_t& reference,
                                        const plan_extent_t& period) {
    const Vec2& p = plan_value (at);
    const Vec2& r = plan_value (reference);
    const Vec2& n = plan_value (period);
    return plan_position (nearest_image (p.x (), r.x (), n.x ()),
                          nearest_image (p.z (), r.z (), n.z ()));
  }

  // The same for a point in space: the world wraps in plan, never in
  // height.
  inline position_t nearest_image (const position_t& at,
                                   const position_t& reference,
                                   const plan_extent_t& period) {
    const Vec3& p = position_value (at);
    const Vec3& r = position_value (reference);
    const Vec2& n = plan_value (period);
    return position (Vec3 (nearest_image (p[0], r[0], n.x ()),
                           p[1],
                           nearest_image (p[2], r[2], n.z ())));
  }

  // ---- Torus coordinate frames (mp-units point origins) ----
  //
  // A place anchored on the periodic world (a bus stop, a landmark, a
  // chart pin) lives on `torus_origin` / `torus_plan_origin`, separate
  // from the unwrapped `world_origin` / `world_plan_origin` that bodies
  // and cameras move in. The two frames never implicitly convert or
  // subtract; crossing between them goes through `point_for` (powered by
  // `mp_units::frame_projection` below) with the world's period and, when
  // entering unwrapped space, the reference point to place it nearest.
  inline constexpr auto world_origin =
    mp_units::default_point_origin (isq::position_vector[si::metre]);
  inline constexpr auto world_plan_origin =
    mp_units::default_point_origin (horizontal_displacement[si::metre]);

  inline constexpr struct torus_origin_t final
      : mp_units::absolute_point_origin<isq::position_vector> {
  } torus_origin;
  inline constexpr struct torus_plan_origin_t final
      : mp_units::absolute_point_origin<horizontal_displacement> {
  } torus_plan_origin;

  using torus_position_t = mp_units::
    quantity_point<isq::position_vector[si::metre], torus_origin, Vec3>;
  using torus_plan_position_t = mp_units::
    quantity_point<horizontal_displacement[si::metre], torus_plan_origin, Vec2>;

  template <class A, class B>
  concept same_frame_difference = requires (A a, B b) { a - b; };
  static_assert (!std::convertible_to<torus_position_t, position_t>);
  static_assert (!std::convertible_to<position_t, torus_position_t>);
  static_assert (!same_frame_difference<torus_position_t, position_t>);
  static_assert (!std::convertible_to<torus_plan_position_t, plan_position_t>);
  static_assert (!std::convertible_to<plan_position_t, torus_plan_position_t>);
  static_assert (
    !same_frame_difference<torus_plan_position_t, plan_position_t>);

  inline torus_position_t torus_position (const Vec3& metres) {
    return torus_origin + metres * isq::position_vector[si::metre];
  }

  inline torus_plan_position_t torus_plan_position (const Vec2& metres) {
    return torus_plan_origin + metres * horizontal_displacement[si::metre];
  }

  inline torus_plan_position_t torus_plan_position (meters_t x, meters_t z) {
    return torus_plan_position (Vec2 (meters_value (x), meters_value (z)));
  }

  inline const Vec3& torus_position_value (const torus_position_t& value) {
    return value.quantity_ref_from (torus_origin)
      .numerical_value_ref_in (si::metre);
  }

  inline const Vec2& torus_plan_value (const torus_plan_position_t& value) {
    return value.quantity_ref_from (torus_plan_origin)
      .numerical_value_ref_in (si::metre);
  }

  inline torus_plan_position_t plan (const torus_position_t& at) {
    return torus_plan_position (moppe::plan (torus_position_value (at)));
  }

  inline torus_position_t at_height (const torus_plan_position_t& at,
                                     meters_t y) {
    return torus_position (Vec3 (torus_plan_value (at).x (),
                                 meters_value (y),
                                 torus_plan_value (at).z ()));
  }

  // Shortest way round the torus when either or both endpoints are torus
  // points.
  inline plan_displacement_t torus_offset (const torus_plan_position_t& from,
                                           const torus_plan_position_t& to,
                                           const plan_extent_t& period) {
    return minimum_image (to - from, period);
  }

  inline plan_displacement_t torus_offset (const plan_position_t& from,
                                           const torus_plan_position_t& to,
                                           const plan_extent_t& period) {
    return minimum_image (
      plan_displacement (torus_plan_value (to) - plan_value (from)), period);
  }

  inline plan_displacement_t torus_offset (const torus_plan_position_t& from,
                                           const plan_position_t& to,
                                           const plan_extent_t& period) {
    return minimum_image (
      plan_displacement (plan_value (to) - torus_plan_value (from)), period);
  }
}

template <>
inline constexpr auto mp_units::frame_projection<moppe::terrain::torus_origin,
                                                 moppe::terrain::world_origin> =
  [] (moppe::terrain::torus_position_t at,
      const moppe::position_t& near,
      const moppe::plan_extent_t& period) {
    return moppe::terrain::nearest_image (
      moppe::position (moppe::terrain::torus_position_value (at)),
      near,
      period);
  };

template <>
inline constexpr auto
  mp_units::frame_projection<moppe::terrain::world_plan_origin,
                             moppe::terrain::torus_plan_origin> =
    [] (moppe::plan_position_t at, const moppe::plan_extent_t& period) {
      return moppe::terrain::torus_plan_position (moppe::Vec2 (
        moppe::plan_value (period).x () > 0.0f
          ? moppe::terrain::wrap_coordinate (moppe::plan_value (at).x (),
                                             moppe::plan_value (period).x ())
          : moppe::plan_value (at).x (),
        moppe::plan_value (period).z () > 0.0f
          ? moppe::terrain::wrap_coordinate (moppe::plan_value (at).z (),
                                             moppe::plan_value (period).z ())
          : moppe::plan_value (at).z ()));
    };

template <>
inline constexpr auto mp_units::frame_projection<moppe::terrain::world_origin,
                                                 moppe::terrain::torus_origin> =
  [] (const moppe::position_t& at, const moppe::plan_extent_t& period) {
    return moppe::terrain::at_height (
      moppe::plan (at).point_for (moppe::terrain::torus_plan_origin, period),
      moppe::position_value (at)[1] * moppe::u::m);
  };

template <>
inline constexpr auto
  mp_units::frame_projection<moppe::terrain::torus_plan_origin,
                             moppe::terrain::world_plan_origin> =
    [] (moppe::terrain::torus_plan_position_t at,
        const moppe::plan_position_t& near,
        const moppe::plan_extent_t& period) {
      return moppe::terrain::nearest_image (
        moppe::plan_position (moppe::terrain::torus_plan_value (at)),
        near,
        period);
    };

#endif
