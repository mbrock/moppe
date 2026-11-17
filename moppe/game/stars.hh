#ifndef MOPPE_GAME_STARS_HH
#define MOPPE_GAME_STARS_HH

#include <moppe/game/game_clock.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
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
        position_t position = moppe::position (Vec3 ());
        // How far it has turned, and how long until it comes back.
        degrees_t phase = 0.0f * u::deg;
        seconds_t respawn = 0.0f * u::s;
      };

      struct State {
        std::array<StarState, MAX_STARS> stars {};
        std::size_t count = 0;
        int collected = 0;
        position_t last_position = position (Vec3 ());
      };

      Stars ();

      void generate (const map::SurfaceGeometry& surface,
                     const WorldParams& params,
                     int count);

      // Checks pickups; returns how many were grabbed this tick
      int update (const position_t& vehicle_pos,
                  play_time_t now,
                  seconds_t interval);

      void render (render::Renderer& r, const FrameEnv& env);

      State state () const;
      void restore (const State& state);

      int collected () const {
        return m_collected;
      }
      const position_t& last_pos () const {
        return m_last_pos;
      }

    private:
      struct Star {
        position_t home;
        position_t pos;
        degrees_t phase;
        seconds_t respawn;
      };

      std::vector<Star> m_stars;
      render::MeshPtr m_body;
      render::MeshPtr m_halo;
      int m_collected;
      position_t m_last_pos = position (Vec3 ());
      plan_extent_t m_period = plan_extent (0.0f * u::m, 0.0f * u::m);
    };
  }
}

#endif
