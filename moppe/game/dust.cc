#include <moppe/game/dust.hh>
#include <moppe/game/sprites.hh>

#include <cmath>

namespace moppe {
namespace game {
  Dust::Dust ()
    : m_particles (),
      m_rng (99)
  { }

  void
  Dust::load (render::Renderer& r) {
    m_tex = make_soft_disc_texture (r);
  }

  void
  Dust::emit (const Vector3D& pos, const Vector3D& vel, int count,
	      const Vector3D& color)
  {
    emit (pos, vel, count, color, Style ());
  }

  void
  Dust::emit (const Vector3D& pos, const Vector3D& vel, int count,
	      const Vector3D& color, const Style& style)
  {
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (int i = 0; i < count && m_particles.size () < 500; ++i) {
      Particle p;
      p.pos = pos + Vector3D (u (m_rng),
			      0.4f * u (m_rng),
			      u (m_rng)) * (0.7f * style.spread);
      p.vel = vel + Vector3D (3.0f * u (m_rng),
			      2.5f + 2.0f * u (m_rng),
			      3.0f * u (m_rng)) * style.spread;
      p.max_life = style.life
	* (0.5f + 0.4f * (0.5f + 0.5f * u (m_rng)));
      p.life = p.max_life;
      p.size = style.size * (0.7f + 0.4f * u (m_rng));
      p.rot = 3.14159f * u (m_rng);
      p.rot_v = 2.2f * u (m_rng);
      p.gravity = style.gravity;
      // Slight per-particle value variation keeps a plume from
      // reading as one flat-colored blob.
      p.color = color * (0.88f + 0.16f * (0.5f + 0.5f * u (m_rng)));
      m_particles.push_back (p);
    }
  }

  void
  Dust::update (float dt)
  {
    size_t j = 0;
    for (size_t i = 0; i < m_particles.size (); ++i) {
      Particle p = m_particles[i];
      p.life -= dt;
      if (p.life <= 0)
	continue;
      p.vel *= std::exp (-2.0f * dt); // air drag
      p.vel.y -= p.gravity * dt;      // clods arc, smoke rises
      p.pos += p.vel * dt;
      p.rot += p.rot_v * dt;
      m_particles[j++] = p;
    }
    m_particles.resize (j);
  }

  void
  Dust::render (render::DrawList& dl, const FrameEnv& env)
  {
    if (m_particles.empty ())
      return;

    // Billboard axes straight from the camera basis
    const Vector3D& right = env.cam_right;
    const Vector3D& up = env.cam_up;

    render::DrawState soft;
    soft.blend = true;
    soft.depth_write = false; // soft puffs, no z-fighting each other
    dl.state (soft);
    dl.lit (false);
    dl.set_texture (m_tex.get ());

    dl.begin (render::Prim::Quads);
    for (size_t i = 0; i < m_particles.size (); ++i) {
      const Particle& p = m_particles[i];
      const float a = p.life / p.max_life;
      const float s = p.size * (1.7f - 0.7f * a); // grow as it fades

      // spin the billboard so puffs tumble
      const float ca = std::cos (p.rot), sa = std::sin (p.rot);
      const Vector3D r2 = (right * ca + up * sa) * s;
      const Vector3D u2 = (up * ca - right * sa) * s;

      // Quick fade-in so puffs don't pop, long fade-out; capped
      // below the old 0.75 so plumes stay translucent.
      const float age = 1.0f - a;
      const float fade_in = std::min (1.0f, age * 8.0f);
      dl.color (p.color, 0.6f * fade_in * a);
      dl.uv (0, 0);
      dl.vertex (p.pos - r2 - u2);
      dl.uv (1, 0);
      dl.vertex (p.pos + r2 - u2);
      dl.uv (1, 1);
      dl.vertex (p.pos + r2 + u2);
      dl.uv (0, 1);
      dl.vertex (p.pos - r2 + u2);
    }
    dl.end ();

    dl.set_texture (0);
    dl.lit (true);
    dl.state (render::DrawState ());
  }
}
}
