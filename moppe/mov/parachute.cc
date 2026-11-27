#include <moppe/mov/parachute.hh>

#include <moppe/mov/box3d_vec.hh>

#include <algorithm>
#include <cmath>

namespace moppe::mov {
  namespace {
    namespace parachute_parts {
      // Parachute flight envelope: gentle, steerable, unmistakably floatier
      // and slower than the hang glider.
      constexpr speed_t cruise_forward = 6.2f * u::m / u::s;
      constexpr speed_t fast_forward = 10.5f * u::m / u::s;
      constexpr speed_t brake_forward = 1.6f * u::m / u::s;
      constexpr speed_t flare_forward = 1.0f * u::m / u::s;

      constexpr speed_t cruise_sink = 3.2f * u::m / u::s;
      constexpr speed_t fast_sink = 4.5f * u::m / u::s;
      constexpr speed_t flare_sink = 1.7f * u::m / u::s;

      constexpr radians_per_second_t turn_rate = 1.85f * u::rad / u::s;
      constexpr seconds_t bloom_time = 0.45f * u::s;

      // Shares the glider's prevailing wind so ridge soaring under canopy
      // matches the world's air.
      const velocity_t wind_velocity = velocity (Vec3 (3.2f, 0.0f, 1.4f));
      const direction_t wind_direction =
        unit_direction (normalized (velocity_value (wind_velocity)));
      constexpr meters_t wind_gradient_height = 25.0f * u::m;
      constexpr rate_of_climb_t ridge_lift_peak =
        4.5f * rate_of_climb_speed[u::m / u::s];
      constexpr meters_t ridge_band = 100.0f * u::m;
      constexpr proportion_t surface_wind_share = 0.3f * proportion[one];
    }
  }

  Parachute::Parachute (const map::SurfaceGeometry& surface)
      : m_surface (surface) {}

  void Parachute::deploy (position_t feet,
                          velocity_t inherited_velocity,
                          const direction_t& heading) {
    m_state = State {};
    m_state.position = feet;
    m_state.velocity = inherited_velocity;
    Vec3 h = direction_value (heading);
    h[1] = 0.0f;
    if (length2 (h) < 1e-4f) {
      const Vec3& v = velocity_value (inherited_velocity);
      h = Vec3 (v[0], 0.0f, v[2]);
    }
    m_state.heading =
      unit_direction (length2 (h) > 1e-4f ? normalized (h) : Vec3 (0, 0, 1));
    m_state.opening = 0.05f * proportion[one];
    m_state.airspeed = length (velocity_value (inherited_velocity)) *
                       moppe::airspeed[u::m / u::s];
    m_state.vertical_speed = float (velocity_value (inherited_velocity)[1]) *
                             rate_of_climb_speed[u::m / u::s];
    m_state.landed = false;
  }

  meters_t Parachute::height_above_ground () const {
    const Vec3& p = position_value (m_state.position);
    return (p[1] - ground_height (m_surface, p[0], p[2])) * u::m;
  }

  rate_of_climb_t Parachute::ridge_lift (meters_t agl) const {
    namespace pp = parachute_parts;
    const Vec3 n =
      normalized (map::ground_normal_at (m_surface, m_state.position)
                    .numerical_value_in (one));
    const proportion_t upslope =
      std::max (0.0f,
                -(n[0] * direction_value (pp::wind_direction)[0] +
                  n[2] * direction_value (pp::wind_direction)[2])) *
      proportion[one];
    const proportion_t height_fade =
      std::clamp (1.0f - (std::max (0.0f * u::m, agl) / pp::ridge_band)
                           .numerical_value_in (one),
                  0.0f,
                  1.0f) *
      proportion[one];
    return pp::ridge_lift_peak * upslope.numerical_value_in (one) *
           height_fade.numerical_value_in (one);
  }

  velocity_t Parachute::air_velocity () const {
    namespace pp = parachute_parts;
    const proportion_t share =
      std::clamp (quantity_cast<proportion> (height_above_ground () /
                                             pp::wind_gradient_height),
                  pp::surface_wind_share,
                  1.0f * proportion[one]);
    velocity_t air = pp::wind_velocity * share.numerical_value_in (one);
    velocity_value (air)[1] =
      m_state.air_mass_lift.numerical_value_in (u::m / u::s);
    return air;
  }

  bool Parachute::update (seconds_t dt, meters_t water_level) {
    namespace pp = parachute_parts;
    State& s = m_state;
    if (s.landed)
      return true;

    // Bloom open smoothly over bloom_time.
    const float opened =
      std::min (1.0f,
                s.opening.numerical_value_in (one) +
                  (dt / pp::bloom_time).numerical_value_in (one));
    s.opening = opened * proportion[one];

    // Steer heading smoothly with A/D (stronger once canopy is inflated).
    const float turn = s.turn.numerical_value_in (one);
    const radians_t yaw_step = -turn * pp::turn_rate * opened * dt;
    Vec3 h = Quaternion::rotate (
      direction_value (s.heading), Vec3 (0, 1, 0), yaw_step);
    h[1] = 0.0f;
    s.heading =
      unit_direction (length2 (h) > 1e-6f ? normalized (h) : Vec3 (0, 0, 1));

    const meters_t agl = height_above_ground ();
    s.air_mass_lift = ridge_lift (agl);
    const velocity_t air = air_velocity ();

    // Desired air-relative velocity under the inflated canopy.
    const float go = s.speed_control.numerical_value_in (one);
    speed_t want_fwd = pp::cruise_forward;
    speed_t want_sink = pp::cruise_sink;
    if (s.flare) {
      want_fwd = pp::flare_forward;
      want_sink = pp::flare_sink;
    } else if (go >= 0.0f) {
      want_fwd =
        pp::cruise_forward + (pp::fast_forward - pp::cruise_forward) * go;
      want_sink = pp::cruise_sink + (pp::fast_sink - pp::cruise_sink) * go;
    } else {
      want_fwd =
        pp::cruise_forward + (pp::brake_forward - pp::cruise_forward) * (-go);
      want_sink =
        pp::cruise_sink + (pp::flare_sink - pp::cruise_sink) * (-go * 0.65f);
    }

    const Vec3 desired_air_vel =
      direction_value (s.heading) * speed_value (want_fwd) -
      Vec3 (0.0f, speed_value (want_sink), 0.0f);
    const Vec3 desired_world = desired_air_vel + velocity_value (air);

    // Aerodynamic canopy drag pulls inherited freefall velocity onto the
    // canopy's steady glide, ramping in rapidly as the chute inflates.
    const float horiz_drag = smoothing_alpha ((1.2f + 2.8f * opened) / u::s, dt)
                               .numerical_value_in (one);
    const float vert_drag =
      smoothing_alpha ((0.8f + 5.8f * opened * opened) / u::s, dt)
        .numerical_value_in (one);
    Vec3& v = velocity_value (s.velocity);
    v[0] += (desired_world[0] - v[0]) * horiz_drag;
    v[2] += (desired_world[2] - v[2]) * horiz_drag;
    v[1] += (desired_world[1] - v[1]) * vert_drag;
    // Before full inflation, gravity still pulls during the opening beat.
    if (opened < 1.0f)
      v[1] -= (1.0f - opened) * 9.81f * seconds_value (dt);

    s.position += quantity_cast<isq::position_vector> (s.velocity * dt);

    const Vec3 through = v - velocity_value (air);
    s.airspeed = length (through) * moppe::airspeed[u::m / u::s];
    s.vertical_speed = float (v[1]) * rate_of_climb_speed[u::m / u::s];

    // Pendulum sway: turning swings the rider outward and tilts the canopy;
    // flaring pitches the canopy back as the pilot swings forward.
    const radians_t roll_target = (0.32f * turn * opened) * u::rad;
    const radians_t pitch_target =
      ((s.flare ? -0.22f : 0.14f * go) * opened) * u::rad;
    const float sway_ease =
      smoothing_alpha (4.2f / u::s, dt).numerical_value_in (one);
    s.sway_roll += (roll_target - s.sway_roll) * sway_ease;
    s.sway_pitch += (pitch_target - s.sway_pitch) * sway_ease;

    // Touchdown on ground or deep water.
    const Vec3& p = position_value (s.position);
    const meters_t ground = ground_height (m_surface, p[0], p[2]) * u::m;
    const meters_t floor = std::max (ground, water_level - 1.25f * u::m);
    if (p[1] * u::m <= floor) {
      position_value (s.position)[1] = meters_value (floor);
      s.landed = true;
    }
    return s.landed;
  }
}
