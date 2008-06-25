#include <moppe/mov/vehicle.hh>
#include <moppe/app/gl.hh>

namespace moppe {
namespace mov {
  static const float radius = 1 * one_meter;

  Vehicle::Vehicle (const Vector3D& position,
		    degrees_t orientation,
		    const HeightMap& map)
    : m_position (position),
      m_orientation (),
      m_yaw (degrees_to_radians (orientation)),
      m_map (map)
  {
    calculate_orientation ();
    fall_to_ground ();
  }

  void
  Vehicle::calculate_orientation ()
  {
    Vector3D n = m_map.interpolated_normal (m_position.x, m_position.z);
    Vector3D flat (std::cos (m_yaw), 0, std::sin (m_yaw));
    m_orientation = flat - n * (flat.dot (n));
    m_orientation.normalize ();
  }

  void
  Vehicle::fall_to_ground ()
  { m_position.y = m_map.interpolated_height (m_position.x, m_position.z)
      + radius; }

  void
  Vehicle::update (seconds_t dt) {
    calculate_orientation ();
    m_position += dt * m_speed * m_orientation;
    fall_to_ground ();
  }

  void
  Vehicle::render () const {
    gl::ScopedMatrixSaver matrix;

    glTranslatef (m_position.x, m_position.y, m_position.z);
    glColor3f (1.0, 0, 0);
    glutSolidSphere (radius, 20, 20);

//     glColor3f (0.5, 1, 0.5);
//     gl::draw_direction (m_orientation);
//     gl::draw_direction (Vector3D (0, -1, 0));
  }
}
}
