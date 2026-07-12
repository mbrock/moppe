#ifndef MOPPE_GAME_DUST_HH
#define MOPPE_GAME_DUST_HH

#include <moppe/game/world.hh>
#include <moppe/render/renderer.hh>

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

      struct State {
        std::vector<render::DustEmission> emissions;
        uint64_t next_id = 1;
        float logical_time = 0.0f;
      };

      void
      emit (const Vec3& pos, const Vec3& vel, int count, DisplayColor color);
      void emit (const Vec3& pos,
                 const Vec3& vel,
                 int count,
                 DisplayColor color,
                 const Style& style);
      void update (float dt);
      void render (render::Renderer& renderer) const;

      State state () const;
      void restore (const State& state);

    private:
      std::vector<render::DustEmission> m_emissions;
      uint64_t m_next_id = 1;
      float m_logical_time = 0.0f;
    };
  }
}

#endif
