#ifndef MOPPE_GLIDER_HH
#define MOPPE_GLIDER_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>

namespace moppe::mov {
  using rate_of_climb_t = quantity<rate_of_climb_speed[u::m / u::s], float>;
  using airspeed_t = quantity<airspeed[u::m / u::s], float>;
  using glide_ratio_t = quantity<glide_ratio[one], float>;

  // A hang glider as a point mass under a wing. The wing makes lift across
  // the air it meets and drag along it, both growing with the square of the
  // airspeed, so height and speed trade for one another: a dive gathers
  // speed that a pull-up gives back as height, a banked wing turns by
  // tilting its lift, and a flare holds the path level while the speed
  // bleeds away, until the wing stalls and settles. The air itself moves -- a
  // prevailing wind that rises over windward slopes -- and the wing flies in
  // it.
  //
  // The pilot's bar sets how hard the wing is loaded (its lift coefficient,
  // here as a fraction of the one that carries the unloaded weight at the
  // reference speed); the flight path follows from the forces. A solo
  // pilot is integrated here. A wing carrying the Box3D bike only supplies
  // its pull (`fly`), and the bike's own world moves the weight, so wheels,
  // suspension, and trunks meet whatever the glide brings them to.
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
      // The wing's loading: 1 carries the pilot alone at the reference
      // speed, more is the bar pushed out.
      float loading = 1.0f;
      control_signal_t turn {};
      control_signal_t speed_control {};
      bool flare {};
      bool bike_attached {};
      bool landed {};
    };

    explicit Glider (const map::SurfaceGeometry& surface);

    void launch (position_t position,
                 velocity_t inherited_velocity,
                 const Vec3& heading,
                 bool bike_attached = false);
    // Flies the wing alone for a step; true once it has come down. The
    // velocity it came down with is kept for whoever takes over.
    bool update (seconds_t dt);

    // For a wing whose load something else moves: takes the load's motion
    // (`position` is the wing's), works the bar and the bank for a step,
    // and returns the acceleration the air gives the whole weight, gravity
    // not included.
    Vec3 fly (seconds_t dt, position_t position, velocity_t velocity);
    // Where that load went.
    void follow (position_t position, velocity_t velocity);
    void land () {
      m_landed = true;
    }

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
    bool drop_bike () {
      const bool was_attached = m_bike_attached;
      m_bike_attached = false;
      return was_attached;
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
    // The way the wing's nose points, along its path through the air and
    // raised by a pushed-out bar.
    Vec3 nose () const;
    // The wing's own up, rolled about the heading by the bank.
    Vec3 up () const;
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
    bool bike_attached () const {
      return m_bike_attached;
    }

    // Steady straight flight at an airspeed, for a pilot alone.
    static rate_of_climb_t polar_sink (airspeed_t airspeed);
    static glide_ratio_t glide_ratio_at (airspeed_t airspeed);

    // How far above the ground the wing is when what hangs from it
    // touches down.
    static constexpr float pilot_clearance = 0.75f;
    static constexpr float bike_hang = 2.4f;

  private:
    rate_of_climb_t ridge_lift (float agl) const;
    float height_above_ground () const;
    Vec3 air_velocity () const;
    float load () const;

    const map::SurfaceGeometry& m_surface;
    position_t m_position {};
    velocity_t m_velocity {};
    Vec3 m_heading { 0, 0, 1 };
    radians_t m_bank {};
    airspeed_t m_airspeed {};
    rate_of_climb_t m_vertical_speed {};
    rate_of_climb_t m_air_mass_lift {};
    float m_loading = 1.0f;
    control_signal_t m_turn {};
    control_signal_t m_speed_control {};
    bool m_flare = false;
    bool m_bike_attached = false;
    bool m_landed = true;
  };
}

#endif
