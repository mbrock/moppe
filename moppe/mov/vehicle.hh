
#ifndef MOPPE_VEHICLE_HH
#define MOPPE_VEHICLE_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>

#include <boost/format.hpp>

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
    Vehicle (const Vector3D& position,
	     degrees_t orientation,
	     const HeightMap& map,
	     magnitude_t max_thrust,
	     magnitude_t mass);

    void render () const;
    void update (seconds_t dt);

    void draw_debug_text () const {
      std::string text =
	(boost::format ("Height: %1% meters. Speed: %2% km/h. "
			"Thrust: %3% N.")
	 % (m_position.y / one_meter)
	 % (((m_velocity.length ()) / 1000.0) * 3600.0)
	 % (m_thrust * m_max_thrust)).str ();
      gl::draw_glut_text (GLUT_BITMAP_HELVETICA_18,
			  20, 60, text);
    }

    void set_thrust (magnitude_t thrust)
    { m_thrust = thrust; }

    void set_yaw (degrees_t degrees)
    { m_yaw_target = degrees_to_radians (degrees); }

    void spin (degrees_t degrees)
    { m_yaw_target += degrees_to_radians (degrees); }

    void increase_thrust (magnitude_t dv)
    { m_thrust += dv; }

    void rocket_jump ();

    void set_water_level (float level)
    { m_water_level = level; }

    void set_obstacles (const std::vector<Box>* boxes)
    { m_obstacles = boxes; }

    // Respawn: back to a spot, stationary, jets cooled down
    void reset (const Vector3D& position)
    {
      m_position = position;
      m_velocity = Vector3D ();
      m_rocket_time = 0;
      m_rocket_cooldown = 0;
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

    // 0..1: how recharged the jump jets are
    float rocket_charge () const;

    void set_camera () const {
      gl::ScopedAttribSaver matrix_mode (GL_TRANSFORM_BIT);
      
      glMatrixMode (GL_MODELVIEW);
      glLoadIdentity ();

      Vector3D p = m_position - m_velocity.normalized () * 10 * one_meter;
      gluLookAt (p.x, p.y + 10 * one_meter, p.z,
		 m_position.x, m_position.y, m_position.z,
		 0, 1, 0);
    }

    Vector3D position    () const { return m_position; }
    Vector3D orientation () const { return m_heading; }
    Vector3D velocity () const { return m_velocity; }

  private:
    void render_car () const;
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
    bool m_rocket_flight;     // landing softened after a rocket

    const HeightMap& m_map;

    const magnitude_t m_max_thrust;
    magnitude_t m_thrust;
    magnitude_t m_mass;

    seconds_t m_rocket_time;
    seconds_t m_rocket_cooldown;
    float m_water_level;

    seconds_t m_airborne_time;
    float m_impact;

    const std::vector<Box>* m_obstacles;

    int m_body_kind;
    Vector3D m_body_color;
  };
}
}

#endif
