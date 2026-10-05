
#ifndef MOPPE_VEHICLE_HH
#define MOPPE_VEHICLE_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/rigid_bike.hh>
#include <moppe/mov/trunk_field.hh>

#include <algorithm>
#include <memory>

namespace moppe {
  namespace mov {
    using namespace moppe::map;

    // Which simulation moves the bike. Classic is the original point mass
    // with a hand-written heading, grip and lean; rigid is the Box3D
    // assemblage of chassis, wheels and suspension in rigid_bike.hh.
    enum class BikePhysics { classic, rigid };
    inline constexpr BikePhysics default_bike_physics = BikePhysics::rigid;

    class Vehicle {
    public:
      struct State {
        position_t position {};
        velocity_t velocity {};
        Vec3 heading {};
        Vec3 thrust_orientation {};
        radians_t yaw {};
        radians_t yaw_target {};
        float lean {};
        Vec3 render_heading {};
        Vec3 render_normal {};
        float susp {};
        float susp_v {};
        float wheel_spin {};
        bool boost_flight {};
        control_signal_t thrust {};
        float boost_input {};
        float boost_drive {};
        float boost_level {};
        float boost_charge {};
        seconds_t boost_recharge_delay {};
        meters_t water_level {};
        seconds_t airborne_time {};
        speed_t impact {};
        meters_t fall_top {};
        meters_t fall_drop {};
        // The rigid assemblage, when that physics drives the bike. Its
        // coarse pose is also published above as position, velocity, and
        // heading; restoring a state whose coarse pose was edited moves the
        // assemblage rigidly to match.
        RigidBikeState rigid {};
        bool parked {};
      };

      // max_thrust caps the wheel force (launch punch); power caps
      // force * speed, so acceleration tapers like a real engine
      // instead of shoving at 3 g all the way to the horizon.
      Vehicle (position_t position,
               degrees_t orientation,
               const SurfaceGeometry& surface,
               newtons_t max_thrust,
               watts_t power,
               kilograms_t mass,
               BikePhysics physics = BikePhysics::classic);
      ~Vehicle ();

      BikePhysics physics () const {
        return m_rigid ? BikePhysics::rigid : BikePhysics::classic;
      }

      void update (seconds_t dt);

      State state () const;
      void restore (const State& state);

      // The throttle is a normalized control signal in [-1, 1] that
      // commands the engine's force capability.
      void set_thrust (control_signal_t thrust) {
        m_thrust = thrust;
      }

      control_signal_t thrust () const {
        return m_thrust;
      }

      void set_yaw (degrees_t degrees) {
        m_yaw_target = degrees;
      }

      void spin (degrees_t degrees) {
        m_yaw_target += radians_t (degrees);
      }

      void increase_thrust (control_signal_t dv) {
        m_thrust += dv;
      }

      // Continuous jump jets.  boost is 0..1; drive is -1..1 and tilts
      // the jet backward/vertical/forward to match the driving stick.
      void set_boost (float boost, float drive);
      void replenish_boost (float amount) {
        m_boost_charge = std::min (1.0f, m_boost_charge + amount);
      }

      // A bike nobody rides stays where it was left: its wheels lock and,
      // once it has settled on the ground, it is held still until mounted.
      void set_parked (bool parked) {
        m_parked = parked;
      }
      bool parked () const {
        return m_parked;
      }

      void set_water_level (meters_t level) {
        m_water_level = level;
      }

      void set_trunks (const TrunkField* trunks) {
        m_trunks = trunks;
        if (m_rigid)
          m_rigid->set_trunks (trunks);
      }

      // Move an inactive bike as a rigid payload beneath the glider.
      void carry (position_t position,
                  velocity_t velocity,
                  const Vec3& heading,
                  const Vec3& up);

      // Respawn: back to a spot, stationary, jets cooled down
      void reset (const Vec3& position);

      void set_heading (const Vec3& h);

      bool grounded () const {
        return m_rigid ? m_rigid->grounded () : is_grounded ();
      }

      // Sideways speed relative to where the bike points; big when
      // drifting, ~zero when rolling straight
      float drift_speed () const {
        const Vec3& v = velocity_value (m_velocity);
        float vf = dot (v, m_heading);
        return length (v - m_heading * vf);
      }

      // Downward speed of the last hard landing; reading it clears it
      float pop_impact () {
        const float value = m_impact.numerical_value_in (u::m / u::s);
        m_impact = 0 * u::m / u::s;
        return value;
      }

      // How far the last flight fell, peak to touchdown, in meters
      float pop_fall_drop () {
        const float value = (m_fall_drop).numerical_value_in (moppe::u::m);
        m_fall_drop = 0 * u::m;
        return value;
      }

      // Stored energy and current output of the continuous jump jets.
      float boost_charge () const {
        return m_boost_charge;
      }
      float boost_level () const {
        return m_boost_level;
      }
      float boost_drive () const {
        return m_boost_drive;
      }

      // Read-only pose and body state for the external renderer
      // (game/vehicle_render); the drawing half reads everything it
      // needs through these.
      radians_t lean () const {
        return m_lean * u::rad;
      }
      float susp () const {
        return m_susp;
      }
      // Accumulated wheel roll angle in [0, 2pi).
      radians_t wheel_spin () const {
        return m_wheel_spin * u::rad;
      }
      bool airborne () const {
        return m_airborne_time > seconds (0.15f);
      }
      float airtime () const {
        return seconds_value (m_airborne_time);
      }
      radians_t yaw () const {
        return m_yaw;
      }
      Vec3 render_normal () const {
        return m_render_normal;
      }
      Vec3 render_orientation () const {
        return m_render_heading;
      }
      // Where the drawn frame stands: the classic bike bobs it on its
      // visual spring, while the rigid chassis is drawn where it is.
      Vec3 render_position () const {
        if (m_rigid)
          return position ();
        return position () + Vec3 (0.0f, m_susp, 0.0f);
      }
      // How far each wheel hangs below its drawn rest position along the
      // chassis' down axis, in metres; negative is compressed.
      float rear_wheel_drop () const {
        return m_rigid ? m_rear_drop : 0.675f * m_susp;
      }
      float front_wheel_drop () const {
        return m_rigid ? m_front_drop : 0.4725f * m_susp;
      }
      // The fork's angle about the steering head; positive turns right.
      radians_t fork_angle () const {
        return m_rigid ? m_fork * u::rad : 0.4f * m_yaw;
      }

      Vec3 position () const {
        return position_value (m_position);
      }
      position_t physical_position () const {
        return m_position;
      }
      Vec3 orientation () const {
        return m_heading;
      }
      Vec3 velocity () const {
        return velocity_value (m_velocity);
      }
      velocity_t physical_velocity () const {
        return m_velocity;
      }

    private:
      void update_jets (seconds_t dt, bool grounded);
      void update_rigid (seconds_t dt);
      void sync_rigid ();
      void steer (seconds_t dt);
      void apply_grip (seconds_t dt, const Vec3& n);
      void calculate_orientation ();
      void fall_to_ground ();
      void check_ground_collision ();
      void collide_with_trunks ();
      void bound ();
      bool expected_landing_pose (Vec3& forward,
                                  Vec3& up,
                                  float& time_to_landing) const;
      bool is_grounded () const;
      bool driving_contact () const;

      acceleration_t drag () const;

      Vec3 ground_normal () const {
        const Vec3& p = position_value (m_position);
        return spatial::sample<terrain::terrain_normal> (
                 m_map, moppe::position (Vec3 (p[0], 0.0f, p[2])))
          .numerical_value_in (mp_units::one);
      }

      float ground_height () const {
        const Vec3& p = position_value (m_position);
        return terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            m_map, moppe::position (Vec3 (p[0], 0.0f, p[2]))));
      }

    private:
      position_t m_position;
      velocity_t m_velocity;
      Vec3 m_heading;
      Vec3 m_thrust_orientation;

      radians_t m_yaw;        // smoothed actual steering
      radians_t m_yaw_target; // raw keyboard input
      float m_lean;           // roll into corners (radians)
      Vec3 m_render_heading;  // visual forward, follows the flight arc
      Vec3 m_render_normal;   // smoothed up vector for drawing
      float m_susp, m_susp_v; // visual suspension spring
      float m_wheel_spin;     // visual wheel roll angle (radians)
      bool m_boost_flight;    // landing softened after using the jets

      const SurfaceGeometry& m_map;

      const newtons_t m_max_thrust;
      const watts_t m_power;
      control_signal_t m_thrust; // throttle command in [-1, 1]
      kilograms_t m_mass;

      float m_boost_input;
      float m_boost_drive;
      float m_boost_level;
      float m_boost_charge;
      seconds_t m_boost_recharge_delay;
      meters_t m_water_level;

      seconds_t m_airborne_time;
      speed_t m_impact;
      meters_t m_fall_top;  // highest point of the current flight
      meters_t m_fall_drop; // set on landing: peak minus touchdown

      const TrunkField* m_trunks = nullptr;

      // Present only for rigid physics, which then owns the motion; the
      // members above are kept as its published readings.
      std::unique_ptr<RigidBike> m_rigid;
      bool m_parked = false;
      float m_rear_drop = 0.0f;
      float m_front_drop = 0.0f;
      float m_fork = 0.0f;
    };
  }
}

#endif
