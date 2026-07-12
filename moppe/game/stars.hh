#ifndef MOPPE_GAME_STARS_HH
#define MOPPE_GAME_STARS_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <array>
#include <cstddef>
#include <vector>

namespace moppe {
  namespace game {
    // Spinning golden pickup stars scattered over the terrain; some
    // hover high enough that only the jump jets reach them.
    class Stars {
    public:
      static constexpr std::size_t MAX_STARS = 250;

      struct StarState {
        Vec3 position {};
        float phase = 0.0f;
        float respawn = 0.0f;
      };

      struct State {
        std::array<StarState, MAX_STARS> stars {};
        std::size_t count = 0;
        int collected = 0;
        Vec3 last_position {};
      };

      Stars ();

      void generate (const map::HeightMap& map,
                     const WorldParams& params,
                     int count);

      // Checks pickups; returns how many were grabbed this tick
      int update (const Vec3& vehicle_pos, float time, float dt);

      void render (render::Renderer& r, const FrameEnv& env);

      State state () const;
      void restore (const State& state);

      int collected () const {
        return m_collected;
      }
      const Vec3& last_pos () const {
        return m_last_pos;
      }

    private:
      struct Star {
        Vec3 home;
        Vec3 pos;
        float phase;
        float respawn;
      };

      std::vector<Star> m_stars;
      render::MeshPtr m_body;
      render::MeshPtr m_halo;
      int m_collected;
      Vec3 m_last_pos;
      Vec3 m_period;
      bool m_periodic = false;
    };
  }
}

#endif
