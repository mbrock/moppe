#include <moppe/game/walker.hh>
#include <moppe/game/door.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
namespace game {
  Walker::Walker ()
    : m_pos (), m_heading (0, 0, 1), m_vy (0), m_turn (0),
      m_walk (0), m_anim (0), m_grounded (true)
  { }

  void
  Walker::spawn (const Vector3D& pos, const Vector3D& heading)
  {
    m_pos = pos;
    m_heading = Vector3D (heading.x, 0, heading.z);
    if (m_heading.length2 () < 0.01f)
      m_heading = Vector3D (0, 0, 1);
    m_heading.normalize ();
    m_vy = 0;
    m_turn = 0;
    m_walk = 0;
  }

  void
  Walker::jump ()
  {
    if (m_grounded)
      m_vy = 5.5f;
  }

  void
  Walker::update (float dt, const map::HeightMap& map,
                  const std::vector<mov::Box>& boxes,
                  const WorldParams& world)
  {
    if (std::abs (m_turn) > 0.01f)
      m_heading = Quaternion::rotate (m_heading, Vector3D (0, 1, 0),
                                      -m_turn * 2.4f * dt);

    float speed = 6.0f;
    if (m_pos.y < world.water_level + 0.5f)
      speed = 2.0f; // wading

    m_pos.x += m_heading.x * m_walk * speed * dt;
    m_pos.z += m_heading.z * m_walk * speed * dt;
    m_anim += std::abs (m_walk) * speed * dt;

    collide (boxes);

    // ground is the terrain, or a roof once we're up on one
    float g = map.interpolated_height (m_pos.x, m_pos.z);
    for (size_t i = 0; i < boxes.size (); ++i) {
      const mov::Box& b = boxes[i];
      if (m_pos.x > b.x0 && m_pos.x < b.x1 &&
          m_pos.z > b.z0 && m_pos.z < b.z1 &&
          m_pos.y > b.top - 1.0f && b.top > g)
        g = b.top;
    }

    m_vy -= 9.82f * dt;
    m_pos.y += m_vy * dt;
    m_grounded = false;
    if (m_pos.y <= g) {
      m_pos.y = g;
      m_vy = 0;
      m_grounded = true;
    }
  }

  void
  Walker::collide (const std::vector<mov::Box>& boxes)
  {
    const float r = 0.4f;
    for (size_t i = 0; i < boxes.size (); ++i) {
      const mov::Box& b = boxes[i];
      if (m_pos.y >= b.top - 0.1f)
        continue; // up on the roof

      const float dx0 = m_pos.x - (b.x0 - r);
      const float dx1 = (b.x1 + r) - m_pos.x;
      const float dz0 = m_pos.z - (b.z0 - r);
      const float dz1 = (b.z1 + r) - m_pos.z;
      if (dx0 <= 0 || dx1 <= 0 || dz0 <= 0 || dz1 <= 0)
        continue;

      // every building has a door in the middle of its +z wall;
      // people fit through, motorcycles do not
      if (Door::in_doorway (b, m_pos.x, m_pos.z))
        continue;

      const float px = std::min (dx0, dx1);
      const float pz = std::min (dz0, dz1);
      if (px < pz)
        m_pos.x = (dx0 < dx1) ? b.x0 - r : b.x1 + r;
      else
        m_pos.z = (dz0 < dz1) ? b.z0 - r : b.z1 + r;
    }
  }

  void
  Walker::render (render::DrawList& dl, float) const
  {
    dl.set_texture (nullptr);

    dl.push ();
    dl.translate (m_pos.x, m_pos.y, m_pos.z);
    dl.rotate_deg (std::atan2 (m_heading.x, m_heading.z) * 57.2958f,
                   0, 1, 0);

    const float swing =
      (std::abs (m_walk) > 0.01f) ? 32.0f * std::sin (m_anim * 3.6f)
                                  : 0.0f;

    // the rider, dismounted: same blue as the bike
    dl.color (0.18f, 0.18f, 0.24f);
    for (int leg = -1; leg <= 1; leg += 2) {
      dl.push ();
      dl.translate (leg * 0.09f, 0.78f, 0);
      dl.rotate_deg (swing * leg, 1, 0, 0);
      dl.translate (0, -0.38f, 0);
      dl.push ();
      dl.scale (0.13f, 0.76f, 0.13f);
      dl.cube (1.0f);
      dl.pop ();
      dl.pop ();
    }

    dl.color (0.15f, 0.5f, 1.0f);
    dl.push ();
    dl.translate (0, 1.14f, 0);
    dl.push ();
    dl.scale (0.38f, 0.6f, 0.22f);
    dl.cube (1.0f);
    dl.pop ();
    dl.pop ();

    for (int arm = -1; arm <= 1; arm += 2) {
      dl.push ();
      dl.translate (arm * 0.25f, 1.38f, 0);
      dl.rotate_deg (-swing * arm * 0.7f, 1, 0, 0);
      dl.translate (0, -0.25f, 0);
      dl.push ();
      dl.scale (0.09f, 0.5f, 0.09f);
      dl.cube (1.0f);
      dl.pop ();
      dl.pop ();
    }

    // blue helmet, naturally: safety first
    dl.color (0.15f, 0.45f, 1.0f);
    dl.push ();
    dl.translate (0, 1.62f, 0);
    dl.sphere (0.15f, 10, 8);
    dl.pop ();

    dl.pop ();
  }
}
}
