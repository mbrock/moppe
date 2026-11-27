#ifndef MOPPE_MOV_PARACHUTE_HH
#define MOPPE_MOV_PARACHUTE_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/glider.hh>

namespace moppe::mov {
  // A steerable personal parachute that the hiker always carries: pull it
  // after jumping off a cliff, bouncing high on the pogo stick, or bailing
  // out of a vehicle or helicopter. It blooms open quickly to brake a fast
  // fall into a calm float, glides gently with W/S, turns smoothly with A/D
  // with pendulum swing underneath, and flares with Space for a feather-soft
  // touchdown.
  class Parachute {
  public:
    // Feet clearance below the harness position (the position tracked here
    // is the hiker's feet, so landing aligns cleanly with terrain/water).
    static constexpr meters_t canopy_height = 4.6f * u::m;

    struct State {
      // The hiker's feet in world space.
      position_t position {};
      velocity_t velocity {};
      direction_t heading = unit_direction (Vec3 (0, 0, 1));
      // 0..1 canopy inflation during the opening bloom.
      proportion_t opening {};
      // Pendulum roll (right positive) and pitch (forward positive).
      radians_t sway_roll {};
      radians_t sway_pitch {};
      airspeed_t airspeed {};
      rate_of_climb_t vertical_speed {};
      rate_of_climb_t air_mass_lift {};
      control_signal_t turn {};
      control_signal_t speed_control {};
      bool flare = false;
      bool landed = true;
    };

    explicit Parachute (const map::SurfaceGeometry& surface);

    void deploy (position_t feet,
                 velocity_t inherited_velocity,
                 const direction_t& heading);

    // Flies the parachute for one step; returns true once the feet meet
    // ground or water at `water_level`.
    bool update (seconds_t dt, meters_t water_level = -1e6f * u::m);

    State state () const {
      return m_state;
    }
    void restore (const State& state) {
      m_state = state;
    }

    void set_turn (control_signal_t turn) {
      m_state.turn = turn;
    }
    void set_speed_control (control_signal_t speed_control) {
      m_state.speed_control = speed_control;
    }
    void set_flare (bool flare) {
      m_state.flare = flare;
    }

    position_t position () const {
      return m_state.position;
    }
    velocity_t velocity () const {
      return m_state.velocity;
    }
    direction_t heading () const {
      return m_state.heading;
    }
    proportion_t opening () const {
      return m_state.opening;
    }
    radians_t sway_roll () const {
      return m_state.sway_roll;
    }
    radians_t sway_pitch () const {
      return m_state.sway_pitch;
    }
    airspeed_t airspeed () const {
      return m_state.airspeed;
    }
    rate_of_climb_t vertical_speed () const {
      return m_state.vertical_speed;
    }
    rate_of_climb_t air_mass_lift () const {
      return m_state.air_mass_lift;
    }
    control_signal_t turn () const {
      return m_state.turn;
    }
    bool flare () const {
      return m_state.flare;
    }
    bool landed () const {
      return m_state.landed;
    }

  private:
    meters_t height_above_ground () const;
    rate_of_climb_t ridge_lift (meters_t agl) const;
    velocity_t air_velocity () const;

    const map::SurfaceGeometry& m_surface;
    State m_state;
  };
}

#endif
