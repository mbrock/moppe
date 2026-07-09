#ifndef MOPPE_GAME_WILDLIFE_HH
#define MOPPE_GAME_WILDLIFE_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/render/draw.hh>

#include <vector>

namespace moppe {
namespace game {
  // Grazing horse herds on the grasslands, circling bird flocks in
  // the sky -- dark birds over land, gulls over the sea.
  class Wildlife {
  public:
    // Rejection-samples herds onto flat grassy meadows and scatters
    // bird flocks anywhere (fixed seed 4242).  Negative counts pick
    // the per-mode defaults: herds 24/3/8 (pico/city/normal),
    // flocks 30 in pico mode, 10 otherwise.
    void generate (const map::HeightMap& map,
		   const WorldParams& params,
		   int herds = -1, int flocks = -1);

    // Purely parametric from env.time; horses cull at 700 m, birds
    // at 1300 m from the camera.
    void render (render::DrawList& dl, const FrameEnv& env) const;

  private:
    struct Horse {
      float x, y, z, heading, phase, size, tone;
    };

    struct Bird {
      float cx, cz, y, radius, speed, offset, flap, size;
      bool gull;
    };

    static void box (render::DrawList& dl,
		     float w, float h, float d);
    static void blob (render::DrawList& dl,
		      float rx, float ry, float rz);
    static void draw_horse (render::DrawList& dl, const Horse& h,
			    float time);
    static void draw_bird (render::DrawList& dl, const Bird& b,
			   float time);

    std::vector<Horse> m_horses;
    std::vector<Bird> m_birds;
  };
}
}

#endif
