
#ifndef MOPPE_VEHICLE_HH
#define MOPPE_VEHICLE_HH

#include <moppe/color.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>

#include <algorithm>
#include <vector>

namespace moppe {
  namespace mov {
    using namespace moppe::map;

    // An axis-aligned solid block (a building): the vehicle bounces
    // off its walls, and its top is drivable ground.
    struct Box {
      float x0, z0, x1, z1, top;
    };

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
        int body_kind {};
        DisplayColor body_color {};
      };

      // max_thrust caps the wheel force (launch punch); power caps
      // force * speed, so acceleration tapers like a real engine
      // instead of shoving at 3 g all the way to the horizon.
      Vehicle (position_t position,
               degrees_t orientation,
               const HeightMap& map,
               newtons_t max_thrust,
               watts_t power,
               kilograms_t mass);

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

      void set_water_level (meters_t level) {
        m_water_level = level;
      }

      void set_obstacles (const std::vector<Box>* boxes) {
        m_obstacles = boxes;
      }

      // Respawn: back to a spot, stationary, jets cooled down
      void reset (const Vec3& position) {
        m_position = moppe::position (position);
        m_velocity = moppe::velocity (Vec3 ());
        m_boost_input = 0;
        m_boost_drive = 0;
        m_boost_level = 0;
        m_boost_charge = 1;
        m_boost_recharge_delay = seconds (0);
        m_boost_flight = false;
        m_impact = 0 * u::m / u::s;
        m_render_heading = m_heading;
        m_render_normal = Vec3 (0, 1, 0);
      }

      void set_heading (const Vec3& h) {
        Vec3 v (h[0], 0, h[2]);
        if (length2 (v) > 0.0001f) {
          normalize (v);
          m_heading = v;
          m_thrust_orientation = v;
        }
      }

      // What this vehicle looks like: 0 = the motorcycle,
      // 1 = civilian car, 2 = police car, 3 = fire truck
      void set_body_style (int kind, DisplayColor color) {
        m_body_kind = kind;
        m_body_color = color;
      }

      bool grounded () const {
        return is_grounded ();
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
        const float value = meters_value (m_fall_drop);
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
      int body_kind () const {
        return m_body_kind;
      }
      DisplayColor body_color () const {
        return m_body_color;
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
      void steer (seconds_t dt);
      void apply_grip (seconds_t dt, const Vec3& n);
      void calculate_orientation ();
      void fall_to_ground ();
      void check_ground_collision ();
      void collide_with_walls ();
      void bound ();
      bool expected_landing_pose (Vec3& forward,
                                  Vec3& up,
                                  float& time_to_landing) const;
      bool is_grounded () const;
      bool driving_contact () const;

      acceleration_t drag () const;

      const Box* roof_under () const;

      Vec3 ground_normal () const {
        if (roof_under ())
          return Vec3 (0, 1, 0);
        const Vec3& p = position_value (m_position);
        return m_map.interpolated_normal (p[0], p[2]);
      }

      float ground_height () const {
        const Box* roof = roof_under ();
        if (roof)
          return roof->top;
        const Vec3& p = position_value (m_position);
        return m_map.interpolated_height (p[0], p[2]);
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

      const HeightMap& m_map;

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

      const std::vector<Box>* m_obstacles;

      int m_body_kind;
      DisplayColor m_body_color;
    };
  }
}

#endif
