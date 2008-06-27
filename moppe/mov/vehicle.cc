#include <moppe/mov/vehicle.hh>
#include <moppe/app/gl.hh>

namespace moppe {
namespace mov {
  static const float radius = 1 * one_meter;

  Vehicle::Vehicle (const Vector3D& position,
		    degrees_t orientation,
		    const HeightMap& map,
		    magnitude_t max_thrust,
		    magnitude_t mass)
    : m_position (position),
      m_velocity (),
      m_yaw (degrees_to_radians (orientation)),
      m_map (map),
      m_max_thrust (max_thrust),
      m_mass (mass)
  {
    calculate_orientation ();
       fall_to_ground ();
  }

  void
  Vehicle::calculate_orientation ()
  {
    //    std::cout << ground_height () - m_position.y << std::endl;

    if (is_grounded ())
      {
	Vector3D n = m_map.interpolated_normal (m_position.x, m_position.z);
	
	Vector3D flat (Quaternion::rotate (m_velocity.normalized (),
					   Vector3D (0, 1, 0),
					   -m_yaw));
	m_thrust_orientation = flat - n * (flat.dot (n));
	m_thrust_orientation.normalize ();
      }
  }

  void
  Vehicle::fall_to_ground ()
  { m_position.y = ground_height (); }

  void
  Vehicle::check_ground_collision ()
  {
    m_position.y = max (ground_height () + radius, m_position.y);
  }

  bool
  Vehicle::is_grounded () const
  {
    return std::abs (ground_height () - m_position.y) <
      (radius + 0.1 * one_meter);
  }

  Vector3D
  Vehicle::drag () const {
    return m_velocity * -0.2;
  }

  void
  Vehicle::update (seconds_t dt) {
    calculate_orientation ();

    Vector3D f;
    const float g = -9.82 * one_meter;
    const Vector3D n = ground_normal ();

    Vector3D o = Quaternion::rotate (-m_velocity.normalized (), n, 180);

    if (is_grounded ())
      {
	f += m_thrust_orientation * m_thrust * m_max_thrust;
	f -= n * g;
      }

    Vector3D a (f / m_mass + drag () + Vector3D (0, g, 0));
    m_velocity += a * dt;

    if (is_grounded ())
      {
	m_velocity -= m_velocity.dot (n) * n;
	//	m_velocity += m_velocity.dot (n) * o * 0.8;
      }

    m_position += m_velocity * dt;

    check_ground_collision ();
    bound ();
  }

  void
  Vehicle::bound () {
    if (!m_map.in_bounds (m_position.x, m_position.z))
      {
	m_velocity = (m_map.center () + Vector3D (0, 1500, 0) - m_position);
	m_velocity.normalize ();
	m_velocity *= (500 / 3.6);
      }
  }

  void
  Vehicle::render () const {
    gl::ScopedMatrixSaver matrix;

    glTranslatef (m_position.x, m_position.y, m_position.z);
    glColor3f (1.0, 0, 0);
    glutSolidSphere (radius, 20, 20);

    glColor3f (0.5, 1, 0.5);
//     gl::draw_direction (m_orientation);
//     gl::draw_direction (m_velocity);
//     gl::draw_direction (Vector3D (0, -1, 0));
    gl::draw_direction (m_thrust_orientation);
  }
}
}
