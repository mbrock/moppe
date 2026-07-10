
#ifndef MOPPE_VEHICLE_HH
#define MOPPE_VEHICLE_HH

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
    // max_thrust caps the wheel force (launch punch); power caps
    // force * speed, so acceleration tapers like a real engine
    // instead of shoving at 3 g all the way to the horizon.
    Vehicle (const Vector3D& position,
	     degrees_t orientation,
	     const HeightMap& map,
	     magnitude_t max_thrust,
	     magnitude_t power,
	     magnitude_t mass);

    void update (seconds_t dt);

    void set_thrust (magnitude_t thrust)
    { m_thrust = thrust; }

    magnitude_t thrust () const { return m_thrust; }

    void set_yaw (degrees_t degrees)
    { m_yaw_target = degrees_to_radians (degrees); }

    void spin (degrees_t degrees)
    { m_yaw_target += degrees_to_radians (degrees); }

    void increase_thrust (magnitude_t dv)
    { m_thrust += dv; }

    // Continuous jump jets.  boost is 0..1; drive is -1..1 and tilts
    // the jet backward/vertical/forward to match the driving stick.
    void set_boost (float boost, float drive);
    void replenish_boost (float amount)
    { m_boost_charge = std::min (1.0f, m_boost_charge + amount); }

    void set_water_level (float level)
    { m_water_level = level; }

    void set_obstacles (const std::vector<Box>* boxes)
    { m_obstacles = boxes; }

    // Respawn: back to a spot, stationary, jets cooled down
    void reset (const Vector3D& position)
    {
      m_position = position;
      m_velocity = Vector3D ();
      m_boost_input = 0;
      m_boost_drive = 0;
      m_boost_level = 0;
      m_boost_charge = 1;
      m_boost_recharge_delay = 0;
      m_boost_flight = false;
      m_impact = 0;
    }

    void set_heading (const Vector3D& h)
    {
      Vector3D v (h.x, 0, h.z);
      if (v.length2 () > 0.0001f)
	{
	  v.normalize ();
	  m_heading = v;
	  m_thrust_orientation = v;
	}
    }

    // What this vehicle looks like: 0 = the motorcycle,
    // 1 = civilian car, 2 = police car, 3 = fire truck
    void set_body_style (int kind, const Vector3D& color)
    {
      m_body_kind = kind;
      m_body_color = color;
    }

    bool grounded () const { return is_grounded (); }

    // Sideways speed relative to where the bike points; big when
    // drifting, ~zero when rolling straight
    float drift_speed () const {
      float vf = m_velocity.dot (m_heading);
      return (m_velocity - m_heading * vf).length ();
    }

    // Downward speed of the last hard landing; reading it clears it
    float pop_impact ()
    { float i = m_impact; m_impact = 0; return i; }

    // How far the last flight fell, peak to touchdown, in meters
    float pop_fall_drop ()
    { float d = m_fall_drop; m_fall_drop = 0; return d; }

    // Stored energy and current output of the continuous jump jets.
    float boost_charge () const { return m_boost_charge; }
    float boost_level () const { return m_boost_level; }
    float boost_drive () const { return m_boost_drive; }

    // Read-only pose and body state for the external renderer
    // (game/vehicle_render); the drawing half reads everything it
    // needs through these.
    float lean () const { return m_lean; }
    float susp () const { return m_susp; }
    // Accumulated wheel roll angle, radians in [0, 2pi).
    float wheel_spin () const { return m_wheel_spin; }
    bool airborne () const { return m_airborne_time > 0.15f; }
    radians_t yaw () const { return m_yaw; }
    Vector3D render_normal () const { return m_render_normal; }
    int body_kind () const { return m_body_kind; }
    Vector3D body_color () const { return m_body_color; }

    Vector3D position    () const { return m_position; }
    Vector3D orientation () const { return m_heading; }
    Vector3D velocity () const { return m_velocity; }

  private:
    void steer (seconds_t dt);
    void apply_grip (seconds_t dt, const Vector3D& n);
    void calculate_orientation ();
    void fall_to_ground ();
    void check_ground_collision ();
    void collide_with_walls ();
    void bound ();
    bool is_grounded () const;
    bool driving_contact () const;

    Vector3D drag () const;

    const Box* roof_under () const;

    Vector3D ground_normal () const {
      if (roof_under ())
	return Vector3D (0, 1, 0);
      return m_map.interpolated_normal (m_position.x, m_position.z);
    }

    float ground_height () const {
      const Box* roof = roof_under ();
      if (roof)
	return roof->top;
      return m_map.interpolated_height (m_position.x, m_position.z);
    }

  private:
    Vector3D m_position;
    Vector3D m_velocity;
    Vector3D m_heading;
    Vector3D m_thrust_orientation;

    radians_t m_yaw;        // smoothed actual steering
    radians_t m_yaw_target; // raw keyboard input
    float m_lean;           // roll into corners (radians)
    Vector3D m_render_normal; // smoothed up vector for drawing
    float m_susp, m_susp_v;   // visual suspension spring
    float m_wheel_spin;       // visual wheel roll angle (radians)
    bool m_boost_flight;      // landing softened after using the jets

    const HeightMap& m_map;

    const magnitude_t m_max_thrust;
    const magnitude_t m_power;
    magnitude_t m_thrust;
    magnitude_t m_mass;

    float m_boost_input;
    float m_boost_drive;
    float m_boost_level;
    float m_boost_charge;
    seconds_t m_boost_recharge_delay;
    float m_water_level;

    seconds_t m_airborne_time;
    float m_impact;
    float m_fall_top;  // highest point of the current flight
    float m_fall_drop; // set on landing: peak minus touchdown

    const std::vector<Box>* m_obstacles;

    int m_body_kind;
    Vector3D m_body_color;
  };
}
}

#endif
