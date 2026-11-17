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

    void Stars::generate (const map::SurfaceGeometry& surface,
                          const WorldParams& params,
                          int count) {
      MOPPE_PROFILE_ZONE ("Stars::generate");
      if (count < 0 || count > static_cast<int> (MAX_STARS))
        throw std::invalid_argument ("star count exceeds supported maximum");
      std::mt19937 rng (555);
      std::uniform_real_distribution<float> u (0.0f, 1.0f);
      m_period = plan_extent (surface.domain ().period_x (),
                              surface.domain ().period_z ());
      const Vec2& size = plan_value (m_period);
      m_collected = 0;

      m_stars.clear ();
      while ((int)m_stars.size () < count) {
        Vec3 at;
        at[0] = size.x () * u (rng);
        at[2] = size.z () * u (rng);

        const meters_t ground =
          terrain::surface_elevation_value (
            spatial::sample<terrain::surface_elevation> (
              surface, moppe::position (Vec3 (at[0], 0.0f, at[2])))) *
          moppe::u::m;
        if (ground < params.water_level + 2.0f * moppe::u::m)
          continue; // land only

        // Every fourth star hangs high up: jump-jet territory
        bool high = (m_stars.size () % 4 == 0);
        const meters_t lift =
          high ? (14.0f + 8.0f * u (rng)) * moppe::u::m : 2.5f * moppe::u::m;
        at[1] = meters_value (ground + lift);
        Star s { .home = moppe::position (at),
                 .pos = moppe::position (at),
                 .phase = 360.0f * u (rng) * moppe::u::deg,
                 .respawn = 0.0f * moppe::u::s };
        m_stars.push_back (s);
      }
    }

    int Stars::update (const position_t& vehicle_at,
                       play_time_t,
                       seconds_t interval) {
      const float dt = seconds_value (interval);
      // The pull's arithmetic runs lane by lane on the plain metres.
      const Vec3& vehicle_pos = position_value (vehicle_at);
      const Vec2& period = plan_value (m_period);
      int picked = 0;
      for (size_t i = 0; i < m_stars.size (); ++i) {
        Star& s = m_stars[i];
        if (s.respawn > 0.0f * u::s) {
          s.respawn -= interval;
          if (s.respawn <= 0.0f * u::s)
            s.pos = s.home;
          continue;
        }
        Vec3& pos = position_value (s.pos);
        Vec3 delta = pos - vehicle_pos;
        delta[0] = terrain::minimum_image_delta (delta[0], period.x ());
        delta[2] = terrain::minimum_image_delta (delta[2], period.z ());
        const meters_t distance = length (delta) * u::m;
        if (distance < 22.0f * u::m && distance > 0.001f * u::m) {
          // Once noticed, a star spirals into the rider. Attraction ramps up
          // smoothly at the edge and wins over the orbit near collection.
          const float pull = 1.0f - meters_value (distance) / 22.0f;
          const blend_weight_t alpha =
            smoothing_alpha ((1.2f + 7.0f * pull * pull) / u::s, dt * u::s);
          Vec3 tangent (-delta[2], 0, delta[0]);
          if (length2 (tangent) > 0.0001f)
            normalize (tangent);
          const speed_t orbit = 4.5f * pull * (1.0f - pull) * (u::m / u::s);
          pos -= delta * alpha.numerical_value_in (one);
          pos += tangent * meters_value (orbit * interval);
          s.phase += dt * (180.0f + 540.0f * pull) * u::deg;
          delta = pos - vehicle_pos;
          delta[0] = terrain::minimum_image_delta (delta[0], period.x ());
          delta[2] = terrain::minimum_image_delta (delta[2], period.z ());
        }
        if (length2 (delta) < 3.0f * 3.0f) {
          s.respawn = 60.0f * u::s; // comes back later
          ++m_collected;
          ++picked;
          m_last_pos = position (vehicle_pos + delta);
        }
      }
      return picked;
    }

    void Stars::render (render::Renderer& r, const FrameEnv& env) {
      const Vec3& cam = position_value (env.camera_pos);
      const Vec2& period = plan_value (m_period);
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

        Vec3 position = position_value (s.pos);
        const float phase = s.phase.numerical_value_in (u::deg);
        position[0] = terrain::nearest_image (position[0], cam[0], period.x ());
        position[2] = terrain::nearest_image (position[2], cam[2], period.z ());
        const float dx = cam[0] - position[0], dz = cam[2] - position[2];
        if (dx * dx + dz * dz > 900.0f * 900.0f)
          continue;

        const Mat4 place = Mat4::translation (
          Vec3 (position[0],
                position[1] + 0.35f * std::sin (time * 2.0f + phase),
                position[2]));
        r.draw_mesh (
          *m_body,
          place * Mat4::rotation ((time * 150.0f + phase) * u::deg, y_axis) *
            Mat4::rotation ((time * 95.0f + phase * 0.7f) * u::deg, x_axis),
          0x100000 + i * 2);

        const float pulse = 1.0f + 0.15f * std::sin (time * 2.0f + phase);
        r.draw_mesh (*m_halo,
                     place * Mat4::scaling (Vec3 (pulse, pulse, pulse)),
                     0x100001 + i * 2);
      }
    }
  }
}
