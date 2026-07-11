#ifndef MOPPE_GAME_DUST_HH
#define MOPPE_GAME_DUST_HH

#include <moppe/game/world.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <random>
#include <vector>

namespace moppe {
  namespace game {
    // Short-lived billboard puffs: drift dust, landing dirt, water
    // spray, roost clods, exhaust smoke, star-pickup sparkles.
    class Dust {
    public:
      // Emission shaping beyond the classic puff.  gravity is a y
      // acceleration: positive values arc dirt clods to the ground,
      // small negative values let smoke rise.
      struct Style {
        float size = 1.0f;     // scales the base particle size
        float life = 1.0f;     // scales the base lifetime
        float gravity = 0.0f;  // m/s^2 downward
        float spread = 1.0f;   // scales the velocity jitter
        bool additive = false; // glow (embers) vs. soft dust
      };

      Dust ();

      void load (render::Renderer& r);

      void emit (const Vector3D& pos,
                 const Vector3D& vel,
                 int count,
                 const Vector3D& color);
      void emit (const Vector3D& pos,
                 const Vector3D& vel,
                 int count,
                 const Vector3D& color,
                 const Style& style);
      void update (float dt);
      void render (render::DrawList& dl, const FrameEnv& env);

    private:
      struct Particle {
        Vector3D pos, vel, color;
        float life, max_life, size, rot, rot_v, gravity;
        bool additive;
      };

      std::vector<Particle> m_particles;
      std::mt19937 m_rng;
      render::TexturePtr m_tex;
    };
  }
}

#endif
