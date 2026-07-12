#include <moppe/game/door.hh>
#include <moppe/game/model.hh>
#include <moppe/game/walker.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    Walker::Walker ()
        : m_pos (), m_heading (0, 0, 1), m_vy (0), m_turn (0), m_walk (0),
          m_anim (0), m_grounded (true) {}

    void Walker::spawn (const Vec3& pos, const Vec3& heading) {
      m_pos = pos;
      m_heading = Vec3 (heading[0], 0, heading[2]);
      if (length2 (m_heading) < 0.01f)
        m_heading = Vec3 (0, 0, 1);
      normalize (m_heading);
      m_vy = 0;
      m_turn = 0;
      m_walk = 0;
    }

    void Walker::jump () {
      if (m_grounded)
        m_vy = 5.5f;
    }

    void Walker::update (float dt,
                         const map::HeightMap& map,
                         const std::vector<mov::Box>& boxes,
                         const WorldParams& world) {
      if (std::abs (m_turn) > 0.01f)
        m_heading = Quaternion::rotate (
          m_heading, Vec3 (0, 1, 0), (-m_turn * 2.4f * dt) * u::rad);

      float speed = 6.0f;
      if (m_pos[1] < meters_value (world.water_level) + 0.5f)
        speed = 2.0f; // wading

      m_pos[0] += m_heading[0] * m_walk * speed * dt;
      m_pos[2] += m_heading[2] * m_walk * speed * dt;
      m_anim += std::abs (m_walk) * speed * dt;

      collide (boxes);

      // ground is the terrain, or a roof once we're up on one
      float g = map.interpolated_height (m_pos[0], m_pos[2]);
      for (size_t i = 0; i < boxes.size (); ++i) {
        const mov::Box& b = boxes[i];
        if (m_pos[0] > b.x0 && m_pos[0] < b.x1 && m_pos[2] > b.z0 &&
            m_pos[2] < b.z1 && m_pos[1] > b.top - 1.0f && b.top > g)
          g = b.top;
      }

      m_vy -= 9.82f * dt;
      m_pos[1] += m_vy * dt;
      m_grounded = false;
      if (m_pos[1] <= g) {
        m_pos[1] = g;
        m_vy = 0;
        m_grounded = true;
      }
    }

    void Walker::collide (const std::vector<mov::Box>& boxes) {
      const float r = 0.4f;
      for (size_t i = 0; i < boxes.size (); ++i) {
        const mov::Box& b = boxes[i];
        if (m_pos[1] >= b.top - 0.1f)
          continue; // up on the roof

        const float dx0 = m_pos[0] - (b.x0 - r);
        const float dx1 = (b.x1 + r) - m_pos[0];
        const float dz0 = m_pos[2] - (b.z0 - r);
        const float dz1 = (b.z1 + r) - m_pos[2];
        if (dx0 <= 0 || dx1 <= 0 || dz0 <= 0 || dz1 <= 0)
          continue;

        // every building has a door in the middle of its +z wall;
        // people fit through, motorcycles do not
        if (Door::in_doorway (b, m_pos[0], m_pos[2]))
          continue;

        const float px = std::min (dx0, dx1);
        const float pz = std::min (dz0, dz1);
        if (px < pz)
          m_pos[0] = (dx0 < dx1) ? b.x0 - r : b.x1 + r;
        else
          m_pos[2] = (dz0 < dz1) ? b.z0 - r : b.z1 + r;
      }
    }

    void Walker::render (render::DrawList& dl,
                         float time,
                         const Vec3& visual_scale) const {
      dl.set_texture (nullptr);

      // The rider, dismounted: the same guy in the same gear, with
      // knees and elbows that actually articulate through the gait.
      const bool moving = std::abs (m_walk) > 0.01f;
      const float ph = m_anim * 3.6f;
      const float bob = moving ? 0.025f * std::fabs (std::sin (ph)) : 0.0f;
      const float breathe = moving ? 0.0f : 0.006f * std::sin (time * 2.0f);

      dl.push ();
      dl.translate (m_pos[0], m_pos[1] + bob, m_pos[2]);
      dl.rotate (std::atan2 (m_heading[0], m_heading[2]) * u::rad, 0, 1, 0);
      dl.scale (visual_scale);

      // Legs: the thigh swings from the hip, the shin lags behind
      // with a knee bend that folds on the back-swing, and the boot
      // rides along.
      for (int s = -1; s <= 1; s += 2) {
        const float p = ph + (s > 0 ? 0.0f : PI);
        const float thigh = moving ? -30.0f * std::sin (p) : 0.0f;
        const float knee =
          moving ? 12.0f + 30.0f * std::max (0.0f, std::sin (p - 1.1f)) : 4.0f;

        dl.push ();
        dl.translate (s * 0.10f, 0.82f, 0);
        dl.rotate (thigh * u::deg, 1, 0, 0);

        model::rider_material (dl, model::RiderMaterial::Pants);
        dl.push ();
        dl.translate (0, -0.20f, 0);
        model::box (dl, 0.13f, 0.42f, 0.14f);
        dl.pop ();

        dl.translate (0, -0.40f, 0);
        dl.rotate (knee * u::deg, 1, 0, 0);
        dl.push ();
        dl.translate (0, -0.17f, 0);
        model::box (dl, 0.11f, 0.36f, 0.12f);
        dl.pop ();

        model::rider_material (dl, model::RiderMaterial::Boots);
        dl.push ();
        dl.translate (0, -0.34f, 0.05f);
        model::box (dl, 0.11f, 0.10f, 0.26f);
        dl.pop ();

        dl.pop ();
      }

      // Pelvis, jersey torso, white roost guard.
      model::rider_material (dl, model::RiderMaterial::Pants);
      dl.push ();
      dl.translate (0, 0.88f, 0);
      model::ellipsoid (dl, 0.16f, 0.11f, 0.12f, 8, 6);
      dl.pop ();

      model::rider_material (dl, model::RiderMaterial::Jersey);
      dl.push ();
      dl.translate (0, 1.24f + breathe, 0);
      model::ellipsoid (dl, 0.18f, 0.24f, 0.13f, 8, 6);
      dl.pop ();
      model::rider_material (dl, model::RiderMaterial::Armor);
      dl.push ();
      dl.translate (0, 1.26f + breathe, 0.09f);
      model::box (dl, 0.15f, 0.20f, 0.05f);
      dl.pop ();

      // Arms counter-swing the legs; the elbow keeps a natural bend
      // and the glove caps the wrist.
      for (int s = -1; s <= 1; s += 2) {
        const float p = ph + (s > 0 ? PI : 0.0f);
        const float swing = moving ? -24.0f * std::sin (p) : 0.0f;

        dl.push ();
        dl.translate (s * 0.24f, 1.42f, 0);
        dl.rotate (swing * u::deg, 1, 0, 0);

        model::rider_material (dl, model::RiderMaterial::Jersey);
        dl.push ();
        dl.translate (0, -0.15f, 0);
        model::box (dl, 0.09f, 0.32f, 0.09f);
        dl.pop ();

        dl.translate (0, -0.30f, 0);
        dl.rotate (
          (moving ? -18.0f - 10.0f * std::max (0.0f, std::sin (p)) : -12.0f) *
            u::deg,
          1,
          0,
          0);
        model::rider_material (dl, model::RiderMaterial::Armor);
        dl.push ();
        dl.translate (0, -0.13f, 0);
        model::box (dl, 0.075f, 0.28f, 0.075f);
        dl.pop ();

        model::rider_material (dl, model::RiderMaterial::Gloves);
        dl.push ();
        dl.translate (0, -0.28f, 0);
        dl.sphere (0.05f, 8, 6);
        dl.pop ();

        dl.pop ();
      }

      // The same lid he wears on the bike: shell, chin bar, dark
      // visor, white peak.
      dl.push ();
      dl.translate (0, 1.62f + breathe, 0);
      model::rider_helmet (dl);
      dl.pop ();

      dl.pop ();
    }
  }
}
