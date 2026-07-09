#include <moppe/mov/vehicle.hh>

#include <cmath>

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
      m_yaw_target (0),
      m_lean (0),
      m_render_normal (0, 1, 0),
      m_susp (0),
      m_susp_v (0),
      m_rocket_flight (false),
      m_map (map),
      m_max_thrust (max_thrust),
      m_thrust (0),
      m_mass (mass),
      m_rocket_time (0),
      m_rocket_cooldown (0),
      m_water_level (-1000 * one_meter),
      m_airborne_time (0),
      m_impact (0),
      m_fall_top (0),
      m_fall_drop (0),
      m_obstacles (0),
      m_body_kind (0),
      m_body_color (0.8, 0.15, 0.1)
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
    // Linear rolling drag plus quadratic air drag: terminal speed
    // lands near the speedometer's 300 km/h, and fall speeds stay
    // survivable
    return m_velocity * -(0.05f + 0.0035f * m_velocity.length ());
  }

  // Grounded, or close enough that a micro-hop over a bump should
  // not cut the throttle -- keeps rough ground feeling planted
  // while real jumps still feel like jumps
  bool
  Vehicle::driving_contact () const {
    if (is_grounded ())
      return true;
    return m_airborne_time < 0.12f &&
      m_position.y - ground_height () < radius + 0.6f;
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

    if (driving_contact ())
      {
	// Full lock turns slower at speed: stable at 250 km/h,
	// nimble at walking pace
	const float vf = std::abs (m_velocity.dot (m_heading));
	const float rate = steering_rate / (1.0f + vf / 70.0f);
	m_heading = Quaternion::rotate (m_heading, ground_normal (),
					-m_yaw * rate * dt);
      }
    else
      // Mid-air attitude control: swing the bike around, keep the
      // momentum -- landing sideways starts a drift
      m_heading = Quaternion::rotate (m_heading, Vector3D (0, 1, 0),
				      -m_yaw * air_steering_rate * dt);
  }

  // Tire grip pulls the velocity into line with where the bike
  // points.  Grip fades continuously with steering input and
  // speed, braking breaks traction outright, and an ongoing slide
  // keeps breathing instead of snapping straight.
  void
  Vehicle::apply_grip (seconds_t dt, const Vector3D& n) {
    Vector3D fwd = m_heading - n * m_heading.dot (n);
    if (fwd.length2 () < 0.000001f)
      return;
    fwd.normalize ();

    // Split velocity into forward, surface-normal, and in-plane
    // lateral parts; only the lateral part is gripped, so a launch
    // (normal component) survives the coyote-contact window
    const float vf = m_velocity.dot (fwd);
    const Vector3D vn = n * m_velocity.dot (n);
    const Vector3D lat = m_velocity - fwd * vf - vn;

    const float steer_amt =
      std::min (1.0f, std::abs (m_yaw) / 0.8f);
    const float speed_amt =
      std::min (1.0f, std::max (0.0f, (std::abs (vf) - 15.0f) / 10.0f));

    float grip = 3.0f - 1.9f * steer_amt * speed_amt;

    if (m_thrust < -0.1f && vf > 3.0f)
      grip = std::min (grip, 0.45f); // brake-slide
    if (lat.length2 () > 16.0f)
      grip = std::min (grip, 1.4f);  // mid-drift hysteresis

    m_velocity = fwd * vf + vn + lat * std::exp (-grip * dt);
  }

  void
  Vehicle::update (seconds_t dt) {
    // Steering input ramps in rather than snapping: smooth onset
    // for the heading, the grip model, and the fork visual at once
    m_yaw += (m_yaw_target - m_yaw)
      * (1.0f - std::exp (-9.0f * dt));

    steer (dt);
    calculate_orientation ();

    const bool contact = driving_contact ();

    Vector3D f;
    const float g = -9.82 * one_meter;
    const Vector3D n = ground_normal ();

    // Thrust stays on through micro-hops (coyote contact); the
    // normal force only applies with real ground under the wheels
    if (contact)
      f += m_thrust_orientation * m_thrust * m_max_thrust;
    if (is_grounded ())
      f -= n * g;

    Vector3D a (f / m_mass + drag () + Vector3D (0, g, 0));

    // The jump jets burn hard enough to climb against gravity and
    // push forward at the same time
    if (m_rocket_time > 0)
      a += Vector3D (0, 35.0f, 0) + m_heading * 10.0f;

    m_velocity += a * dt;

    // Sticking to the ground would cancel the rockets, so traction
    // is suspended while they burn
    if (m_rocket_time <= 0)
      {
	if (is_grounded ())
	  m_velocity -= m_velocity.dot (n) * n;
	if (contact)
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
	  {
	    m_impact = std::max (0.0f,
				 -m_velocity.dot (ground_normal ()));
	    // Rocket landings are partly forgiven: the jets flare
	    // on touchdown, or so the story goes
	    if (m_rocket_flight)
	      m_impact *= 0.75f;
	    m_susp_v -= 0.10f * m_impact;
	    m_fall_drop = m_fall_top - m_position.y;
	  }
	m_rocket_flight = false;
	m_airborne_time = 0;
	m_fall_top = m_position.y;
      }
    else
      {
	m_airborne_time += dt;
	m_fall_top = std::max (m_fall_top, m_position.y);
      }

    // Lean into corners: balance the turn against gravity
    {
      float target = 0;
      if (driving_contact ())
	{
	  const float vf = m_velocity.dot (m_heading);
	  const float rate =
	    steering_rate / (1.0f + std::abs (vf) / 70.0f);
	  target = std::atan2 (vf * (-m_yaw * rate), 9.82f);
	  target = std::max (-0.7f, std::min (0.7f, target));
	}
      m_lean += (target - m_lean) * (1.0f - std::exp (-8.0f * dt));
    }

    // Smoothed up vector for drawing: the raw bilinear normal
    // jitters cell-to-cell at speed
    m_render_normal =
      linear_vector_interpolate (m_render_normal, ground_normal (),
				 1.0f - std::exp (-10.0f * dt));
    if (m_render_normal.length2 () > 0.000001f)
      m_render_normal.normalize ();

    // Visual suspension spring: kicked by landings, settles fast
    m_susp_v += (-70.0f * m_susp - 9.0f * m_susp_v) * dt;
    m_susp += m_susp_v * dt;
    m_susp = std::max (-0.35f, std::min (0.15f, m_susp));
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
    m_rocket_flight = true;
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

}
}
