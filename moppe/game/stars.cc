#include <moppe/game/stars.hh>

#include <moppe/profile.hh>

#include <cmath>
#include <random>
#include <stdexcept>

namespace moppe {
  namespace game {
    Stars::Stars () : m_collected (0) {}

    Stars::State Stars::state () const {
      if (m_stars.size () > MAX_STARS)
        throw std::logic_error ("star set exceeds snapshot capacity");
      State result;
      result.count = m_stars.size ();
      result.collected = m_collected;
      result.last_position = m_last_pos;
      for (std::size_t i = 0; i < m_stars.size (); ++i)
        result.stars[i] = { m_stars[i].pos,
                            m_stars[i].phase,
                            m_stars[i].respawn };
      return result;
    }

    void Stars::restore (const State& state) {
      if (state.count != m_stars.size ())
        throw std::invalid_argument (
          "star state does not match the generated star set");
      m_collected = state.collected;
      m_last_pos = state.last_position;
      for (std::size_t i = 0; i < state.count; ++i) {
        m_stars[i].pos = state.stars[i].position;
        m_stars[i].phase = state.stars[i].phase;
        m_stars[i].respawn = state.stars[i].respawn;
      }
    }

    void Stars::generate (const map::HeightMap& map,
                          const WorldParams& params,
                          int count) {
      MOPPE_PROFILE_ZONE ("Stars::generate");
      if (count < 0 || count > static_cast<int> (MAX_STARS))
        throw std::invalid_argument ("star count exceeds supported maximum");
      std::mt19937 rng (555);
      std::uniform_real_distribution<float> u (0.0f, 1.0f);
      const Vec3 size = map.size ();
      m_period = size;
      m_periodic = map.periodic ();
      m_collected = 0;

      m_stars.clear ();
      while ((int)m_stars.size () < count) {
        Star s;
        s.pos[0] = size[0] * (m_periodic ? u (rng) : 0.03f + 0.94f * u (rng));
        s.pos[2] = size[2] * (m_periodic ? u (rng) : 0.03f + 0.94f * u (rng));

        float ground = map.interpolated_height (s.pos[0], s.pos[2]);
        if (ground < meters_value (params.water_level) + 2)
          continue; // land only

        // Every fourth star hangs high up: jump-jet territory
        bool high = (m_stars.size () % 4 == 0);
        s.pos[1] = ground + (high ? 14.0f + 8.0f * u (rng) : 2.5f);
        s.home = s.pos;
        s.phase = 360.0f * u (rng);
        s.respawn = 0;
        m_stars.push_back (s);
      }
    }

    int Stars::update (const Vec3& vehicle_pos, float, float dt) {
      int picked = 0;
      for (size_t i = 0; i < m_stars.size (); ++i) {
        Star& s = m_stars[i];
        if (s.respawn > 0) {
          s.respawn -= dt;
          if (s.respawn <= 0)
            s.pos = s.home;
          continue;
        }
        Vec3 delta = s.pos - vehicle_pos;
        if (m_periodic) {
          delta[0] = terrain::minimum_image_delta (delta[0], m_period[0]);
          delta[2] = terrain::minimum_image_delta (delta[2], m_period[2]);
        }
        const float distance = length (delta);
        if (distance < 22.0f && distance > 0.001f) {
          // Once noticed, a star spirals into the rider. Attraction ramps up
          // smoothly at the edge and wins over the orbit near collection.
          const float pull = 1.0f - distance / 22.0f;
          const float alpha =
            smoothing_alpha ((1.2f + 7.0f * pull * pull) / u::s, dt * u::s);
          Vec3 tangent (-delta[2], 0, delta[0]);
          if (length2 (tangent) > 0.0001f)
            normalize (tangent);
          const float orbit = 4.5f * pull * (1.0f - pull);
          s.pos -= delta * alpha;
          s.pos += tangent * (orbit * dt);
          s.phase += dt * (180.0f + 540.0f * pull);
          delta = s.pos - vehicle_pos;
          if (m_periodic) {
            delta[0] = terrain::minimum_image_delta (delta[0], m_period[0]);
            delta[2] = terrain::minimum_image_delta (delta[2], m_period[2]);
          }
        }
        if (length2 (delta) < 3.0f * 3.0f) {
          s.respawn = 60.0f; // comes back later
          ++m_collected;
          ++picked;
          m_last_pos = vehicle_pos + delta;
        }
      }
      return picked;
    }

    void Stars::render (render::Renderer& r, const FrameEnv& env) {
      const Vec3& cam = position_value (env.camera_pos);
      const float time = seconds_value (env.time);

      // One shared ring-and-core mesh and one halo mesh, baked on the
      // first frame; each visible star is then just two draw calls.
      if (!m_body) {
        render::DrawList dl;
        dl.lit (false);
        dl.color (1.0f, 0.85f, 0.15f);
        dl.torus (0.16f, 0.75f, 10, 18);
        dl.color (1.0f, 0.95f, 0.6f);
        dl.sphere (0.3f, 8, 8);
        m_body = r.create_mesh (dl);

        // A breathing additive halo turns each pickup into a beacon
        // (the bloom pass picks it up from far off); the per-star
        // pulse rides on the model matrix.
        dl.clear ();
        dl.lit (false);
        render::DrawState glow;
        glow.blend = true;
        glow.additive = true;
        glow.depth_write = false;
        dl.state (glow);
        dl.color (1.0f, 0.80f, 0.30f, 0.15f);
        dl.sphere (1.15f, 10, 8);
        m_halo = r.create_mesh (dl);
      }

      const Vec3 y_axis (0, 1, 0), x_axis (1, 0, 0);
      for (size_t i = 0; i < m_stars.size (); ++i) {
        const Star& s = m_stars[i];
        if (s.respawn > 0)
          continue;

        Vec3 position = s.pos;
        if (m_periodic) {
          position[0] =
            terrain::nearest_image (position[0], cam[0], m_period[0]);
          position[2] =
            terrain::nearest_image (position[2], cam[2], m_period[2]);
        }
        const float dx = cam[0] - position[0], dz = cam[2] - position[2];
        if (dx * dx + dz * dz > 900.0f * 900.0f)
          continue;

        const Mat4 place = Mat4::translation (
          Vec3 (position[0],
                position[1] + 0.35f * std::sin (time * 2.0f + s.phase),
                position[2]));
        r.draw_mesh (
          *m_body,
          place * Mat4::rotation ((time * 150.0f + s.phase) * u::deg, y_axis) *
            Mat4::rotation ((time * 95.0f + s.phase * 0.7f) * u::deg, x_axis));

        const float pulse = 1.0f + 0.15f * std::sin (time * 2.0f + s.phase);
        r.draw_mesh (*m_halo,
                     place * Mat4::scaling (Vec3 (pulse, pulse, pulse)));
      }
    }
  }
}
