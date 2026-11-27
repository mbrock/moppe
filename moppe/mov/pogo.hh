#ifndef MOPPE_MOV_POGO_HH
#define MOPPE_MOV_POGO_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

#include <vector>

namespace moppe::mov {
  // What a rider on the pogo stick asks this step: a horizontal velocity
  // direction/magnitude to bounce toward and whether Space is held to pump
  // the spring harder for comically huge bounces.
  struct PogoIntent {
    velocity_t wish_velocity {};
    bool pump = false;
  };

  // A slightly overpowered mechanical pogo stick: a spring-loaded telescoping
  // pole with footpegs and a heavy coil spring. Touching down compresses the
  // spring and stores energy before boinging back into the air. Moving along
  // carries faster than running, and holding Space pumps consecutive bounces
  // higher and faster, while landing from any height converts the fall into
  // a gigantic spring rebound.
  class Pogo {
  public:
    // Maximum cruise speeds (unpumped vs fully pumped super-boing).
    static constexpr speed_t cruise_speed = 13.5f * u::m / u::s;
    static constexpr speed_t super_speed = 19.0f * u::m / u::s;
    static constexpr meters_t radius = 0.32f * u::m;
    static constexpr meters_t height = 2.0f * u::m;
    static constexpr meters_t spring_travel = 0.44f * u::m;
    static constexpr metres_per_second_squared_t gravity =
      15.5f * u::m / pow<2> (u::s);

    struct State {
      // Bottom of the rubber bounce tip.
      position_t position {};
      velocity_t velocity {};
      direction_t heading = unit_direction (Vec3 (0, 0, 1));
      direction_t ground_normal = unit_direction (Vec3 (0, 1, 0));
      // 0..1: how far the coil spring is currently squished.
      proportion_t compression {};
      // True while the rubber tip is planted and the spring is loading.
      bool compressing = false;
      // Time spent in the current compression phase.
      seconds_t compress_time {};
      // Target duration of the current compression before release.
      seconds_t compress_duration = 0.14f * u::s;
      // Stored vertical launch speed waiting for spring release.
      speed_t stored_launch {};
      // Stored horizontal boost direction from slope/impact.
      velocity_t stored_horizontal {};
      // Consecutive pumped bounce charge, 0..1: rises on each pumped
      // bounce for comically escalating height and speed.
      proportion_t charge {};
      // Current tilt of the stick: pitch forward, roll right.
      radians_t pitch {};
      radians_t roll {};
      seconds_t airborne_time {};
      // Set on the single tick a bounce launches, for dust/splash/shake.
      speed_t last_launch {};
      bool just_bounced = false;
      bool just_landed = false;
      speed_t impact_speed {};
    };

    void spawn (position_t feet,
                const direction_t& heading,
                velocity_t inherited = moppe::velocity (Vec3 ()));

    void turn_by (radians_t angle);
    void set_turn (control_signal_t turn) {
      m_turn = turn;
    }
    void set_drive (control_signal_t drive) {
      m_drive = drive;
    }
    void set_strafe (control_signal_t strafe) {
      m_strafe = strafe;
    }
    void set_pump (bool pump) {
      m_pump = pump;
    }

    void update (seconds_t dt,
                 const map::SurfaceGeometry& surface,
                 const TrunkField* obstacles = nullptr);

    State state () const {
      return m_state;
    }
    void restore (const State& state) {
      m_state = state;
    }

    const position_t& position () const {
      return m_state.position;
    }
    const velocity_t& velocity () const {
      return m_state.velocity;
    }
    direction_t heading () const {
      return m_state.heading;
    }
    proportion_t compression () const {
      return m_state.compression;
    }
    proportion_t charge () const {
      return m_state.charge;
    }
    radians_t pitch () const {
      return m_state.pitch;
    }
    radians_t roll () const {
      return m_state.roll;
    }
    bool grounded () const {
      return m_state.compressing;
    }
    bool just_bounced () const {
      return m_state.just_bounced;
    }
    bool just_landed () const {
      return m_state.just_landed;
    }
    speed_t last_launch () const {
      return m_state.last_launch;
    }
    speed_t impact_speed () const {
      return m_state.impact_speed;
    }
    seconds_t airborne_time () const {
      return m_state.airborne_time;
    }
    // Rider's first-person eye height above the rubber tip, dipping when
    // the spring squishes.
    position_t eye_position () const;

  private:
    struct Support {
      bool found = false;
      meters_t height {};
      direction_t normal = unit_direction (Vec3 (0, 1, 0));
    };

    Support probe (const position_t& tip,
                   meters_t reach_down,
                   const map::SurfaceGeometry& surface,
                   const TrunkField* obstacles) const;
    void slide (position_t& tip,
                const displacement_t& delta,
                const map::SurfaceGeometry& surface,
                const TrunkField* obstacles);

    State m_state;
    control_signal_t m_turn {};
    control_signal_t m_drive {};
    control_signal_t m_strafe {};
    bool m_pump = false;
    std::vector<MoverPlane> m_planes;
  };
}

#endif
