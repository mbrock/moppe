#ifndef MOPPE_GLIDER_HH
#define MOPPE_GLIDER_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>

namespace moppe::mov {
  using rate_of_climb_t = quantity<rate_of_climb_speed[u::m / u::s], float>;
  using airspeed_t = quantity<airspeed[u::m / u::s], float>;
  using glide_ratio_t = quantity<glide_ratio[one], float>;

  // A deliberately compact soaring model.  The glider is described by a
  // speed-to-sink polar rather than by generic thrust: the air mass supplies
  // ridge lift, the wing always sinks through it, and bank trades height for
  // turn rate.
  class Glider {
  public:
    struct State {
      position_t position {};
      velocity_t velocity {};
      Vec3 heading { 0, 0, 1 };
      radians_t bank {};
      airspeed_t airspeed {};
      rate_of_climb_t vertical_speed {};
      rate_of_climb_t air_mass_lift {};
      control_signal_t turn {};
      control_signal_t speed_control {};
      bool flare {};
      bool landed {};
    };

    explicit Glider (const map::Surface& surface);

    void launch (position_t position,
                 velocity_t inherited_velocity,
                 const Vec3& heading);
    bool update (seconds_t dt);

    State state () const;
    void restore (const State& state);

    void set_turn (control_signal_t turn) {
      m_turn = turn;
    }
    void set_speed_control (control_signal_t speed_control) {
      m_speed_control = speed_control;
    }
    void set_flare (bool flare) {
      m_flare = flare;
    }

    Vec3 position () const {
      return position_value (m_position);
    }
    position_t physical_position () const {
      return m_position;
    }
    Vec3 velocity () const {
      return velocity_value (m_velocity);
    }
    velocity_t physical_velocity () const {
      return m_velocity;
    }
    const Vec3& heading () const {
      return m_heading;
    }
    radians_t bank () const {
      return m_bank;
    }
    airspeed_t airspeed () const {
      return m_airspeed;
    }
    rate_of_climb_t vertical_speed () const {
      return m_vertical_speed;
    }
    rate_of_climb_t air_mass_lift () const {
      return m_air_mass_lift;
    }
    bool landed () const {
      return m_landed;
    }

    static rate_of_climb_t polar_sink (airspeed_t airspeed);
    static glide_ratio_t glide_ratio_at (airspeed_t airspeed);

  private:
    rate_of_climb_t ridge_lift () const;
    void bound ();

    const map::Surface& m_surface;
    position_t m_position {};
    velocity_t m_velocity {};
    Vec3 m_heading { 0, 0, 1 };
    radians_t m_bank {};
    airspeed_t m_airspeed {};
    rate_of_climb_t m_vertical_speed {};
    rate_of_climb_t m_air_mass_lift {};
    control_signal_t m_turn {};
    control_signal_t m_speed_control {};
    bool m_flare = false;
    bool m_landed = true;
  };
}

#endif
