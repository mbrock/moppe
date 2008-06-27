
#ifndef MOPPE_VEHICLE_HH
#define MOPPE_VEHICLE_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>

#include <boost/format.hpp>

namespace moppe {
namespace mov {
  using namespace moppe::map;

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
    { m_yaw = degrees_to_radians (degrees); }

    void spin (degrees_t degrees)
    { m_yaw += degrees_to_radians (degrees); }

    void increase_thrust (magnitude_t dv)
    { m_thrust += dv; }

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
    Vector3D orientation () const { return m_velocity.normalized (); }

  private:
    void calculate_orientation ();
    void fall_to_ground ();
    void check_ground_collision ();
    void bound ();
    bool is_grounded () const;

    Vector3D drag () const;

    Vector3D ground_normal () const
    { return m_map.interpolated_normal (m_position.x, m_position.z); }

    float ground_height () const
    { return m_map.interpolated_height (m_position.x, m_position.z); }

  private:
    Vector3D m_position;
    Vector3D m_velocity;
    Vector3D m_thrust_orientation;

    radians_t m_yaw;

    const HeightMap& m_map;

    const magnitude_t m_max_thrust;
    magnitude_t m_thrust;
    magnitude_t m_mass;
  };
}
}

#endif
