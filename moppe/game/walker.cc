#include <moppe/game/walker.hh>

#include <cmath>

namespace moppe {
  namespace game {
    Walker::Walker ()
        : m_pos (moppe::position (Vec3 ())), m_heading (0, 0, 1),
          m_vy (0 * isq::velocity[u::m / u::s]), m_turn (0), m_walk (0),
          m_strafe (0), m_run (false), m_anim (0 * u::m), m_grounded (true) {}

    void Walker::turn_by (float radians) {
      m_heading =
        Quaternion::rotate (m_heading, Vec3 (0, 1, 0), -radians * u::rad);
      m_heading[1] = 0.0f;
      normalize (m_heading);
    }

    void Walker::spawn (position_t pos, const Vec3& heading) {
      m_pos = pos;
      m_heading = Vec3 (heading[0], 0, heading[2]);
      if (length2 (m_heading) < 0.01f)
        m_heading = Vec3 (0, 0, 1);
      normalize (m_heading);
      m_vy = 0 * isq::velocity[u::m / u::s];
      m_turn = 0;
      m_walk = 0;
      m_strafe = 0;
    }

    void Walker::jump () {
      if (m_grounded)
        m_vy = 5.5f * isq::velocity[u::m / u::s];
    }

    void Walker::update (seconds_t dt,
                         const map::SurfaceGeometry& surface,
                         const WorldParams& world,
                         const mov::TrunkField* trunks) {
      const float turn = scalar_value (m_turn);
      const float walk = scalar_value (m_walk);
      if (std::abs (turn) > 0.01f)
        m_heading = Quaternion::rotate (
          m_heading, Vec3 (0, 1, 0), -turn * 2.4f * u::rad / u::s * dt);

      // A brisk walk, or a run with Shift held; wading slows either.
      const Vec3& p = position_value (m_pos);
      speed_t speed = (m_run ? 8.5f : 4.2f) * u::m / u::s;
      if (p[1] * u::m < world.water_level + 0.5f * u::m)
        speed = 2.0f * u::m / u::s;

      const Vec3 right (m_heading[2], 0.0f, -m_heading[0]);
      Vec3 step = m_heading * walk - right * scalar_value (m_strafe);
      const float amount = std::min (1.0f, length (step));
      if (amount > 0.0f)
        step = normalized (step) * amount;
      m_pos += quantity_cast<isq::position_vector> (step * (speed * dt));
      m_anim += amount * speed * dt;

      if (trunks) {
        // Knee to head: a walker steps around trunks and slides along them.
        Vec3& feet = position_value (m_pos);
        const mov::TrunkContact contact = trunks->collide (
          feet + Vec3 (0.0f, 0.6f, 0.0f), feet + Vec3 (0.0f, 1.5f, 0.0f), 0.3f);
        if (contact.hit)
          feet += Vec3 (contact.push[0], 0.0f, contact.push[2]);
      }

      Vec3& position = position_value (m_pos);
      const float g = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (position[0], 0.0f, position[2]))));

      m_vy -= 9.82f * isq::acceleration[u::m / pow<2> (u::s)] * dt;
      position[1] += (m_vy * dt).numerical_value_in (u::m);
      m_grounded = false;
      if (position[1] <= g) {
        position[1] = g;
        m_vy = 0 * isq::velocity[u::m / u::s];
        m_grounded = true;
      }
    }
  }
}
