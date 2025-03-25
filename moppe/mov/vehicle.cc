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
      m_thrust_orientation(),
      m_avg_orientation(),
      m_yaw (degrees_to_radians (orientation)),
      m_target_yaw (degrees_to_radians (orientation)),
      m_skid_factor (0.0f),
      m_turn_rate (0.0f),
      m_skid_direction (),
      m_map (map),
      m_max_thrust (max_thrust),
      m_thrust (0.0f),
      m_mass (mass),
      m_headlight_on(true),
      m_is_first_update(true)
  {
    calculate_orientation ();
    fall_to_ground ();
  }

  void
  Vehicle::calculate_orientation ()
  {
    if (is_grounded ())
      {
        // Get surface normal at current position
        Vector3D n = m_map.interpolated_normal (m_position.x, m_position.z);
        
        // Create target orientation based on velocity or yaw
        Vector3D target_orientation;
        
        if (m_velocity.length() > 0.1) {
          // Use velocity for thrust direction when moving
          target_orientation = m_velocity.normalized();
        } else {
          // When not moving, use yaw to determine direction
          target_orientation = Vector3D(cos(m_yaw), 0, sin(m_yaw));
        }
        
        // Smoothly blend orientations over time
        float alpha = 1.0f; // m_is_first_update ? 1.0f : 0.1f;
        m_avg_orientation = linear_vector_interpolate(m_avg_orientation, target_orientation, alpha);
        m_avg_orientation.normalize();
        
        // Project onto ground plane
        m_thrust_orientation = m_avg_orientation - n * (m_avg_orientation.dot(n));
        
        // Ensure non-zero thrust direction
        if (m_thrust_orientation.length() < 0.01) {
          m_thrust_orientation = Vector3D(1, 0, 0);
        }
        
        m_thrust_orientation.normalize();
        m_is_first_update = false;
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
    // Increase the grounding tolerance to prevent "stuck in mud" effect
    return std::abs (ground_height () - m_position.y) <
      (radius + 0.3 * one_meter);
  }

  Vector3D
  Vehicle::drag () const {
    // Much lower base drag factor for better acceleration
    float drag_factor = 0.01f;
    
    // Different drag in different directions
    Vector3D velocity_dir = m_velocity.normalized();
    
    // Apply directional drag - less in forward direction
    if (m_velocity.length() > 0.1f && is_grounded()) {
      // Get thrust direction on horizontal plane
      Vector3D thrust_dir(cos(m_yaw), 0, sin(m_yaw));
      
      // Calculate dot product to see how aligned velocity is with thrust
      float alignment = velocity_dir.dot(thrust_dir);
      
      // If aligned with thrust direction and accelerating, apply minimal drag
      if (alignment > 0.7f && m_thrust > 0.5f) {
        drag_factor = 0.005f; // Very low drag when accelerating forward
      }
      
      // Apply lateral drag for skidding
      if (m_skid_factor > 0.05f) {
        Vector3D lateral = m_skid_direction.normalized();
        float lateral_speed = m_velocity.dot(lateral);
        
        // Only drag perpendicular to motion direction
        return velocity_dir * -drag_factor * m_velocity.length() +
               lateral * -lateral_speed * m_skid_factor * 0.05f;
      }
    }
    
    return m_velocity * -drag_factor;
  }

  void
  Vehicle::update (seconds_t dt) {
    calculate_orientation ();

    Vector3D f;
    const float g = -9.82 * one_meter;
    const Vector3D n = ground_normal ();

    // Update skidding mechanics
    if (is_grounded ())
      {
        // Calculate movement speed
        float speed = m_velocity.length();
              
        // Apply a much stronger thrust multiplier
        float thrust_multiplier = 2.0f; // Base multiplier is 3x higher
                
        f += m_thrust_orientation * m_thrust * m_max_thrust * thrust_multiplier;
        f -= n * g;
      }

    // Calculate acceleration
    Vector3D drag_force = drag();
    
    // Reduce drag during initial acceleration to prevent "stuck in mud" effect
    if (is_grounded() && m_thrust > 0.5f && m_velocity.length() < 15.0f) {
      drag_force = drag_force * 0.2f; // 80% reduction in drag during initial acceleration
    }

    Vector3D a (f / m_mass + drag_force + Vector3D (0, g, 0));
    m_velocity += a * dt;

    if (is_grounded ())
      {
        m_velocity -= m_velocity.dot (n) * n;
        // Maintain momentum better on slopes
        if (n.y < 0.9f) { // If on a slope
          Vector3D slope_dir = n.cross(Vector3D(0, 1, 0)).cross(n).normalized();
          float slope_factor = (1.0f - n.y) * 2.0f; // Steeper slope = more downhill acceleration
          m_velocity += slope_dir * slope_factor * 9.82f * dt; // Add gravity-based acceleration along slope
        }
      }

    m_position += m_velocity * dt;

    bound ();
    check_ground_collision ();
  }

  void
  Vehicle::bound () {
    if (!m_map.in_bounds (m_position.x, m_position.z))
      {
	m_velocity = (m_map.center () + Vector3D (0, 1500, 0) - m_position);
	m_velocity.normalize ();
	m_velocity *= (400 / 3.6);
      }
  }

  void
  Vehicle::render () const {
    gl::ScopedMatrixSaver matrix;

    // Move to vehicle position
    glTranslatef (m_position.x, m_position.y, m_position.z);

    // Draw velocity vector indicator
    glColor3f (0.5, 1, 0.5);
//    gl::draw_direction (m_velocity);
    gl::draw_direction (m_thrust_orientation);

    
    // Ensure depth testing is enabled
    glEnable(GL_DEPTH_TEST);
    
    Vector3D forward;
    Vector3D up;
    
    // Use the averaged orientation instead of direct velocity
    // This provides smoother transitions
    forward = m_avg_orientation.normalized();
    
    // When in the air (jumping/flying), allow proper pitch
    if (!is_grounded()) {
      // Standard up vector for in-air orientation
      up = Vector3D(0, 1, 0);
    } else {
      // When grounded, use ground normal as up
      up = ground_normal();
    }
    
    // Apply correction angle of -90 degrees
    float cosAngle = cos(-M_PI/2);
    float sinAngle = sin(-M_PI/2);
    Vector3D correctedForward(
      forward.x * cosAngle - forward.z * sinAngle,
      forward.y,
      forward.x * sinAngle + forward.z * cosAngle
    );
    forward = correctedForward;
    
    // Create a proper orthonormal basis for orientation
    Vector3D right = forward.cross(up).normalized();
    // Recalculate up to ensure orthogonality
    up = right.cross(forward).normalized();
    
    // Build rotation matrix manually
    GLfloat rotMatrix[16] = {
      right.x, right.y, right.z, 0,
      up.x, up.y, up.z, 0,
      -forward.x, -forward.y, -forward.z, 0,  // Negative forward to face correct way
      0, 0, 0, 1
    };
    
    // Apply rotation matrix
    glMultMatrixf(rotMatrix);
    
    // Disable face culling - we'll draw all surfaces
    glDisable(GL_CULL_FACE);
    
    // Render motorcycle-style vehicle using solid geometry
    
    // Main body (box)
    glColor3f(0.8, 0.0, 0.0); // Red
    
    // Body is a simple box
    glPushMatrix();
    glScalef(radius * 2.0, radius * 0.5, radius * 0.5); // Scale to make it elongated
    glutSolidCube(1.0);
    glPopMatrix();
    
    // Front wheel
    glPushMatrix();
    glColor3f(0.1, 0.1, 0.1); // Dark gray/black
    glTranslatef(radius * 1.0, -radius * 0.7, 0); // Position front wheel
    
    // Draw a solid torus for the wheel
    glutSolidTorus(radius * 0.1, radius * 0.4, 10, 16);
    glPopMatrix();
    
    // Rear wheel
    glPushMatrix();
    glColor3f(0.1, 0.1, 0.1); // Dark gray/black
    glTranslatef(-radius * 1.0, -radius * 0.7, 0); // Position rear wheel
    
    // Draw a solid torus for the wheel
    glutSolidTorus(radius * 0.1, radius * 0.4, 10, 16);
    glPopMatrix();
    
    // Draw connecting elements - front fork
    glPushMatrix();
    glColor3f(0.5, 0.5, 0.5); // Gray
    glTranslatef(radius * 1.0, -radius * 0.3, 0);
    glRotatef(90, 1, 0, 0);
    // Replace glutSolidCylinder with a scaled cube for the fork
    glScalef(radius * 0.1, radius * 0.1, radius * 0.8);
    glutSolidCube(1.0);
    glPopMatrix();
    
    // Rear suspension
    glPushMatrix();
    glColor3f(0.5, 0.5, 0.5); // Gray
    glTranslatef(-radius * 1.0, -radius * 0.3, 0);
    glRotatef(90, 1, 0, 0);
    // Replace glutSolidCylinder with a scaled cube for the suspension
    glScalef(radius * 0.1, radius * 0.1, radius * 0.8);
    glutSolidCube(1.0);
    glPopMatrix();
    
    // Headlight (when on)
    if (m_headlight_on) {
      glPushMatrix();
      glTranslatef(radius * 1.2, 0, 0);
      glColor3f(1.0, 1.0, 0.7); // Yellowish
      glutSolidSphere(radius * 0.2, 10, 10);
      glPopMatrix();
    }
  }
}
}
