#ifndef MOPPE_GAME_GAME_CLOCK_HH
#define MOPPE_GAME_GAME_CLOCK_HH

// The two clocks a session keeps (GameLogicState): play's, in seconds
// since it began, and the day's, in hours since the first midnight. Each
// is a point on a scale of its own, so a moment on one is never taken for
// a moment on the other; the time between two moments on one is a
// duration. Both count in double so that long sessions keep their steps.

#include <moppe/gfx/math.hh>

#include <concepts>

namespace moppe::game {
  inline constexpr struct play_began final
      : mp_units::absolute_point_origin<mp_units::isq::time> {
  } play_began;
  inline constexpr struct first_midnight final
      : mp_units::absolute_point_origin<mp_units::isq::time> {
  } first_midnight;

  using play_time_t =
    mp_units::quantity_point<mp_units::si::second, play_began, double>;
  using clock_time_t =
    mp_units::quantity_point<mp_units::si::hour, first_midnight, double>;
  using duration_t = mp_units::quantity<mp_units::si::second, double>;

  // Neither clock's moments pass for the other's, nor measure from them.
  template <class A, class B>
  concept measured_from = requires (A a, B b) { a - b; };
  static_assert (!std::convertible_to<play_time_t, clock_time_t>);
  static_assert (!measured_from<play_time_t, clock_time_t>);
  static_assert (measured_from<play_time_t, play_time_t>);

  // The moment `seconds` into play, and `hours` after the first midnight.
  constexpr play_time_t play_time (double seconds) {
    return play_began + seconds * mp_units::si::second;
  }
  constexpr clock_time_t clock_time (double hours) {
    return first_midnight + hours * mp_units::si::hour;
  }

  // The same as plain numbers, for what reads time as one: the shaders'
  // time, a sway's phase, the almanac and the weather.
  constexpr double seconds_into_play (play_time_t t) {
    return t.quantity_from (play_began)
      .numerical_value_in (mp_units::si::second);
  }
  constexpr double hours_since_midnight (clock_time_t t) {
    return t.quantity_from (first_midnight)
      .numerical_value_in (mp_units::si::hour);
  }

  // A duration as the float seconds the rest of the game counts in.
  constexpr float seconds_between (play_time_t later, play_time_t earlier) {
    return static_cast<float> (
      (later - earlier).numerical_value_in (mp_units::si::second));
  }
}

#endif
