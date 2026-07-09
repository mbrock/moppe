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
  // spray, and star-pickup sparkles.
  class Dust {
  public:
    Dust ();

    void load (render::Renderer& r);

    void emit (const Vector3D& pos, const Vector3D& vel, int count,
	       const Vector3D& color);
    void update (float dt);
    void render (render::DrawList& dl, const FrameEnv& env);

  private:
    struct Particle {
      Vector3D pos, vel, color;
      float life, max_life, size, rot, rot_v;
    };

    std::vector<Particle> m_particles;
    std::mt19937 m_rng;
    render::TexturePtr m_tex;
  };
}
}

#endif
