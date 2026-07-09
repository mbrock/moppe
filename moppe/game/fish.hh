#ifndef MOPPE_GAME_FISH_HH
#define MOPPE_GAME_FISH_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/render/draw.hh>

#include <vector>

namespace moppe {
namespace game {
  // Schools of little fish circling in the deeper water.
  class Fish {
  public:
    // Rejection-samples school anchors into water at least 6 m
    // deep (fixed seed 777).  schools < 0 picks the per-mode
    // default: 40 in pico mode, 16 otherwise.
    void generate (const map::HeightMap& map,
		   const WorldParams& params,
		   int schools = -1);

    // Purely parametric from env.time; schools further than 600 m
    // from the camera are skipped (invisible underwater anyway).
    void render (render::DrawList& dl, const FrameEnv& env) const;

  private:
    struct One {
      float cx, cz, y, radius, speed, phase, size, hue;
    };

    std::vector<One> m_fish;
  };
}
}

#endif
