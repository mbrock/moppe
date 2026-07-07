#include <moppe/mov/vehicle.hh>
#include <moppe/app/gl.hh>

namespace moppe {
namespace mov {
  static const float radius = 1 * one_meter;

  // How fast full steering input swings the bike itself, in radians
  // per second per radian of yaw input.  Grip then drags the
  // velocity around after the heading.
  static const float steering_rate = 1.6;
  static const float air_steering_rate = 0.9;

  static const seconds_t rocket_burn_time = 1.0;
  static const seconds_t rocket_cooldown_time = 2.2;

  Vehicle::Vehicle (const Vector3D& position,
		    degrees_t orientation,
		    const HeightMap& map,
		    magnitude_t max_thrust,
		    magnitude_t mass)
    : m_position (position),
      m_velocity (),
      m_heading (std::sin (degrees_to_radians (orientation)),
		 0,
		 std::cos (degrees_to_radians (orientation))),
      m_thrust_orientation (m_heading),
      m_yaw (0),
      m_map (map),
      m_max_thrust (max_thrust),
      m_mass (mass),
      m_rocket_time (0),
      m_rocket_cooldown (0),
      m_water_level (-1000 * one_meter),
      m_airborne_time (0),
      m_impact (0),
      m_obstacles (0)
  {
    calculate_orientation ();
       fall_to_ground ();
  }

  void
  Vehicle::calculate_orientation ()
  {
    if (is_grounded ())
      {
	Vector3D n = m_map.interpolated_normal (m_position.x, m_position.z);

	// Keep the heading tangent to the ground; the heading itself
	// is steered explicitly, and grip drags the velocity along,
	// so a heading/velocity mismatch is a drift, not an error.
	Vector3D heading = m_heading - n * (m_heading.dot (n));

	if (heading.length2 () > 0.0001f)
	  {
	    heading.normalize ();
	    m_heading = heading;
	  }

	m_thrust_orientation = m_heading;
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
    return m_velocity * -0.05;
  }

  // The obstacle box whose roof is the effective ground under the
  // bike -- only counts once the bike is up at roof level, so a
  // building towering overhead is not "ground".
  const Box*
  Vehicle::roof_under () const {
    if (!m_obstacles)
      return 0;

    const Box* found = 0;
    float best = m_map.interpolated_height (m_position.x,
					    m_position.z);

    for (size_t i = 0; i < m_obstacles->size (); ++i)
      {
	const Box& b = (*m_obstacles)[i];
	if (m_position.x >= b.x0 && m_position.x <= b.x1 &&
	    m_position.z >= b.z0 && m_position.z <= b.z1 &&
	    m_position.y > b.top - 2 * radius &&
	    b.top > best)
	  {
	    best = b.top;
	    found = &b;
	  }
      }

    return found;
  }

  void
  Vehicle::collide_with_walls () {
    if (!m_obstacles)
      return;

    for (size_t i = 0; i < m_obstacles->size (); ++i)
      {
	const Box& b = (*m_obstacles)[i];

	if (m_position.y - radius >= b.top - 0.05f)
	  continue; // on or above the roof

	const float dx0 = m_position.x - (b.x0 - radius);
	const float dx1 = (b.x1 + radius) - m_position.x;
	const float dz0 = m_position.z - (b.z0 - radius);
	const float dz1 = (b.z1 + radius) - m_position.z;

	if (dx0 <= 0 || dx1 <= 0 || dz0 <= 0 || dz1 <= 0)
	  continue; // clear of this block

	// Push out along the axis of least penetration and bounce;
	// a hard bonk registers as an impact for shake and dust
	const float px = std::min (dx0, dx1);
	const float pz = std::min (dz0, dz1);

	if (px < pz)
	  {
	    m_position.x = (dx0 < dx1) ? b.x0 - radius : b.x1 + radius;
	    m_impact = std::max (m_impact,
				 0.4f * std::abs (m_velocity.x));
	    m_velocity.x *= -0.35f;
	  }
	else
	  {
	    m_position.z = (dz0 < dz1) ? b.z0 - radius : b.z1 + radius;
	    m_impact = std::max (m_impact,
				 0.4f * std::abs (m_velocity.z));
	    m_velocity.z *= -0.35f;
	  }
      }
  }

  void
  Vehicle::steer (seconds_t dt) {
    if (std::abs (m_yaw) < 0.001f)
      return;

    if (is_grounded ())
      m_heading = Quaternion::rotate (m_heading, ground_normal (),
				      -m_yaw * steering_rate * dt);
    else
      // Mid-air attitude control: swing the bike around, keep the
      // momentum -- landing sideways starts a drift
      m_heading = Quaternion::rotate (m_heading, Vector3D (0, 1, 0),
				      -m_yaw * air_steering_rate * dt);
  }

  // Tire grip pulls the velocity into line with where the bike
  // points.  Braking hard or flicking the bars at speed breaks
  // traction, and the bike slides -- that's a drift.
  void
  Vehicle::apply_grip (seconds_t dt, const Vector3D& n) {
    Vector3D fwd = m_heading - n * m_heading.dot (n);
    if (fwd.length2 () < 0.000001f)
      return;
    fwd.normalize ();

    const float vf = m_velocity.dot (fwd);
    const Vector3D lat = m_velocity - fwd * vf;

    float grip = 3.0f;
    if (std::abs (m_yaw) > 0.01f && std::abs (vf) > 20.0f)
      grip = 1.1f;
    if (m_thrust < -0.1f && vf > 3.0f)
      grip = 0.45f;

    m_velocity = fwd * vf + lat * std::exp (-grip * dt);
  }

  void
  Vehicle::update (seconds_t dt) {
    steer (dt);
    calculate_orientation ();

    Vector3D f;
    const float g = -9.82 * one_meter;
    const Vector3D n = ground_normal ();

    if (is_grounded ())
      {
	f += m_thrust_orientation * m_thrust * m_max_thrust;
	f -= n * g;
      }

    Vector3D a (f / m_mass + drag () + Vector3D (0, g, 0));

    // The jump jets burn hard enough to climb against gravity and
    // push forward at the same time
    if (m_rocket_time > 0)
      a += Vector3D (0, 35.0f, 0) + m_heading * 10.0f;

    m_velocity += a * dt;

    // Sticking to the ground would cancel the rockets, so traction
    // is suspended while they burn
    if (is_grounded () && m_rocket_time <= 0)
      {
	m_velocity -= m_velocity.dot (n) * n;
	apply_grip (dt, n);
      }

    // Wading through the ocean is slow going
    if (m_position.y - radius < m_water_level)
      m_velocity *= std::exp (-1.4 * dt);

    m_position += m_velocity * dt;

    if (m_rocket_time > 0)
      m_rocket_time -= dt;
    if (m_rocket_cooldown > 0)
      m_rocket_cooldown -= dt;

    bound ();
    check_ground_collision ();
    collide_with_walls ();

    // Landing detection, for camera shake and dust bursts.  What
    // matters is the speed INTO the surface at touchdown -- landing
    // parallel to a downhill slope is gentle no matter how fast the
    // descent was.
    if (is_grounded ())
      {
	if (m_airborne_time > 0.25f)
	  m_impact = std::max (0.0f,
			       -m_velocity.dot (ground_normal ()));
	m_airborne_time = 0;
      }
    else
      m_airborne_time += dt;
  }

  float
  Vehicle::rocket_charge () const {
    if (m_rocket_cooldown <= 0)
      return 1;
    return 1 - m_rocket_cooldown / rocket_cooldown_time;
  }

  void
  Vehicle::rocket_jump () {
    if (m_rocket_cooldown > 0)
      return;

    // Fires on the ground or mid-air; the initial kick gets the
    // bike off the deck and update() keeps the burn going
    m_velocity += Vector3D (0, 12.0f, 0);
    m_rocket_time = rocket_burn_time;
    m_rocket_cooldown = rocket_cooldown_time;
  }

  void
  Vehicle::bound () {
    if (!m_map.in_bounds (m_position.x, m_position.z))
      {
	m_velocity = (m_map.center () + Vector3D (0, 1500, 0) - m_position);
	m_velocity.normalize ();
	m_velocity *= (400 / 3.6);

	// Face the direction we got flung so the bike doesn't sail
	// home backwards.
	Vector3D level (m_velocity.x, 0, m_velocity.z);
	if (level.length2 () > 0.0001f)
	  {
	    level.normalize ();
	    m_heading = level;
	    m_thrust_orientation = level;
	  }
      }
  }

  static void
  solid_box (float w, float h, float d) {
    glPushMatrix ();
    glScalef (w, h, d);
    glutSolidCube (1.0);
    glPopMatrix ();
  }

  static void
  solid_blob (float rx, float ry, float rz) {
    glPushMatrix ();
    glScalef (rx, ry, rz);
    glutSolidSphere (1.0, 16, 16);
    glPopMatrix ();
  }

  // A wheel rolling along +z: torus rotated so its axis lies on x.
  static void
  draw_wheel (float y, float z) {
    gl::ScopedMatrixSaver matrix;
    glTranslatef (0, y, z);
    glRotatef (90, 0, 1, 0);
    glColor3f (0.08, 0.08, 0.1);
    glutSolidTorus (0.13, 0.32, 10, 18);
    glColor3f (0.7, 0.72, 0.78);
    glutSolidSphere (0.13, 10, 10);
  }

  void
  Vehicle::render () const {
    gl::ScopedMatrixSaver matrix;
    gl::ScopedAttribSaver attribs (GL_ENABLE_BIT | GL_LIGHTING_BIT |
				   GL_CURRENT_BIT | GL_TEXTURE_BIT);

    // The terrain leaves its textures enabled on units 0-3; with no
    // texture coordinates they modulate everything toward black.
    for (int unit = 3; unit >= 0; --unit)
      {
	glActiveTexture (GL_TEXTURE0 + unit);
	glDisable (GL_TEXTURE_2D);
      }

    glTranslatef (m_position.x, m_position.y, m_position.z);

    // Orient the bike along its heading, upright on the terrain.
    Vector3D fwd   = m_heading.normalized ();
    Vector3D right = ground_normal ().cross (fwd).normalized ();
    Vector3D up    = fwd.cross (right);

    const float frame[16] = { right.x, right.y, right.z, 0,
			      up.x,    up.y,    up.z,    0,
			      fwd.x,   fwd.y,   fwd.z,   0,
			      0,       0,       0,       1 };
    glMultMatrixf (frame);

    // Chunkier bike, easier to see from the chase camera; lifted so
    // the scaled wheels still touch the ground
    glTranslatef (0, 0.5, 0);
    glScalef (1.5, 1.5, 1.5);

    // Track glColor as the material so each part has its own color.
    glEnable (GL_COLOR_MATERIAL);
    glColorMaterial (GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    static const float gloss[] = { 0.9f, 0.9f, 0.9f, 1.0f };
    glMaterialfv (GL_FRONT_AND_BACK, GL_SPECULAR, gloss);
    glMaterialf (GL_FRONT_AND_BACK, GL_SHININESS, 90.0f);

    draw_wheel (-0.55, -0.75);

    // Gas tank and frame in glorious metallic blue.
    glColor3f (0.15, 0.5, 1.0);
    {
      gl::ScopedMatrixSaver m;
      glTranslatef (0, -0.15, 0.1);
      solid_blob (0.24, 0.3, 0.7);
    }

    // Seat.
    glColor3f (0.08, 0.08, 0.1);
    {
      gl::ScopedMatrixSaver m;
      glTranslatef (0, -0.02, -0.5);
      solid_box (0.3, 0.12, 0.55);
    }

    // Exhaust pipes.
    glColor3f (0.75, 0.78, 0.8);
    for (int s = -1; s <= 1; s += 2)
      {
	gl::ScopedMatrixSaver m;
	glTranslatef (s * 0.17, -0.5, -0.25);
	solid_box (0.09, 0.09, 0.85);
      }

    // Steering assembly: forks, handlebars, headlight, front wheel.
    {
      gl::ScopedMatrixSaver steering;
      glTranslatef (0, 0.05, 0.55);
      glRotatef (-m_yaw * 0.4f * (180.0f / 3.14159f), 0, 1, 0);

      glColor3f (0.75, 0.78, 0.8);
      {
	gl::ScopedMatrixSaver m;
	glTranslatef (0, -0.3, 0.1);
	glRotatef (-18, 1, 0, 0);
	solid_box (0.08, 0.75, 0.08);
      }

      // Handlebars.
      glColor3f (0.1, 0.1, 0.12);
      {
	gl::ScopedMatrixSaver m;
	glTranslatef (0, 0.12, 0);
	solid_box (0.8, 0.06, 0.06);
      }

      // Headlight, drawn unlit so it always looks switched on.
      {
	gl::ScopedMatrixSaver m;
	glDisable (GL_LIGHTING);
	glColor3f (1.0, 0.95, 0.7);
	glTranslatef (0, -0.02, 0.2);
	glutSolidSphere (0.09, 10, 10);
	glEnable (GL_LIGHTING);
      }

      draw_wheel (-0.6, 0.2);
    }

    // The fearless rider: leaning torso, stubby arms, blue helmet.
    glColor3f (0.15, 0.2, 0.35);
    {
      gl::ScopedMatrixSaver m;
      glTranslatef (0, 0.3, -0.35);
      glRotatef (-15, 1, 0, 0);
      solid_blob (0.2, 0.34, 0.16);
    }
    for (int s = -1; s <= 1; s += 2)
      {
	gl::ScopedMatrixSaver m;
	glTranslatef (s * 0.21, 0.33, 0.12);
	glRotatef (22, 1, 0, 0);
	solid_box (0.07, 0.07, 0.8);
      }
    glColor3f (0.15, 0.45, 1.0);
    {
      gl::ScopedMatrixSaver m;
      glTranslatef (0, 0.64, -0.18);
      glutSolidSphere (0.17, 12, 12);
    }
    glColor3f (0.05, 0.05, 0.08);
    {
      gl::ScopedMatrixSaver m;
      glTranslatef (0, 0.62, -0.02);
      solid_box (0.16, 0.08, 0.1);
    }

    // Jump-jet nozzles under the frame, pointing at the ground.
    glColor3f (0.6, 0.62, 0.68);
    for (int s = -1; s <= 1; s += 2)
      {
	gl::ScopedMatrixSaver m;
	glTranslatef (s * 0.14, -0.45, -0.35);
	glRotatef (90, 1, 0, 0);
	glutSolidCone (0.09, 0.22, 8, 2);
      }

    // Exhaust flames while the throttle is open.
    if (std::abs (m_thrust) > 0.1)
      {
	glDisable (GL_LIGHTING);
	glColor3f (1.0, 0.55, 0.1);
	for (int s = -1; s <= 1; s += 2)
	  {
	    gl::ScopedMatrixSaver m;
	    glTranslatef (s * 0.17, -0.5, -0.7);
	    glRotatef (180, 0, 1, 0);
	    glutSolidCone (0.07, 0.2 + 0.35 * std::abs (m_thrust),
			   8, 2);
	  }
	glEnable (GL_LIGHTING);
      }

    // Rocket blast while the jump jets burn: an orange plume with
    // a blue-white core, shrinking as the burn runs out.
    if (m_rocket_time > 0)
      {
	const float k = m_rocket_time / rocket_burn_time;

	glDisable (GL_LIGHTING);
	for (int s = -1; s <= 1; s += 2)
	  {
	    gl::ScopedMatrixSaver m;
	    glTranslatef (s * 0.14, -0.55, -0.35);
	    glRotatef (90, 1, 0, 0);
	    glColor3f (1.0, 0.7, 0.15);
	    glutSolidCone (0.14, 1.8 * k, 8, 2);
	    glColor3f (0.55, 0.75, 1.0);
	    glutSolidCone (0.08, 2.6 * k, 8, 2);
	  }
      }
  }
}
}
