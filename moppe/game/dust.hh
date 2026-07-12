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
        meters_t size = 1.0f * u::m;
        seconds_t lifetime = 1.0f * u::s;
        acceleration_component_t downward_acceleration =
          0.0f * isq::acceleration[u::m / pow<2> (u::s)];
        magnitude_t spread = 1.0f * one;
        bool additive = false; // glow (embers) vs. soft dust
      };

      Dust ();

      struct Emission {
        uint64_t id = 0;
        seconds_t birth_time {};
        position_t position {};
        velocity_t velocity {};
        DisplayColor color;
        Style style;
        uint32_t particle_count = 0;
      };

      struct State {
        std::vector<Emission> emissions;
        uint64_t next_id = 1;
        seconds_t logical_time {};
      };

      void emit (position_t pos, velocity_t vel, int count, DisplayColor color);
      void emit (position_t pos,
                 velocity_t vel,
                 int count,
                 DisplayColor color,
                 const Style& style);
      void update (seconds_t dt);
      void render (render::Renderer& renderer) const;

      State state () const;
      void restore (const State& state);

    private:
      std::vector<Emission> m_emissions;
      uint64_t m_next_id = 1;
      seconds_t m_logical_time = 0.0f * u::s;
    };
  }
}

#endif
