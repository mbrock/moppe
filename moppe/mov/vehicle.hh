
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
	     const HeightMap& map);

    void render () const;
    void update (seconds_t dt);

    void draw_debug_text () const {
      std::string text =
	(boost::format ("Orientation: %1%.  Position: %2%.")
	 % m_orientation % m_position).str ();
      gl::draw_glut_text (GLUT_BITMAP_HELVETICA_18,
			  20, 60, text);
    }

    void set_speed (magnitude_t meters_per_second)
    { m_speed = meters_per_second; }
    void spin (degrees_t degrees)
    { m_yaw += degrees_to_radians (degrees); }

    void set_camera () const {
      gl::ScopedAttribSaver matrix_mode (GL_TRANSFORM_BIT);
      
      glMatrixMode (GL_MODELVIEW);
      glLoadIdentity ();

      Vector3D p = m_position - m_orientation * 10 * one_meter;
      gluLookAt (p.x, p.y + 10 * one_meter, p.z,
		 m_position.x, m_position.y, m_position.z,
		 0, 1, 0);
    }

    Vector3D position    () const { return m_position; }
    Vector3D orientation () const { return m_orientation; }

  private:
    void calculate_orientation ();
    void fall_to_ground ();

  private:
    Vector3D m_position;
    Vector3D m_orientation;

    radians_t m_yaw;
    float m_speed;

    const HeightMap& m_map;
  };
}
}

#endif
