#ifndef MOPPE_GAME_STARS_HH
#define MOPPE_GAME_STARS_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/render/draw.hh>

#include <vector>

namespace moppe {
namespace game {
  // Spinning golden pickup stars scattered over the terrain; some
  // hover high enough that only the jump jets reach them.
  class Stars {
  public:
    Stars ();

    void generate (const map::HeightMap& map,
		   const WorldParams& params, int count);

    // Checks pickups; returns how many were grabbed this tick
    int update (const Vector3D& vehicle_pos, float time, float dt);

    void render (render::DrawList& dl, const FrameEnv& env);

    int collected () const { return m_collected; }
    const Vector3D& last_pos () const { return m_last_pos; }

  private:
    struct Star {
      Vector3D pos;
      float phase;
      float respawn;
    };

    std::vector<Star> m_stars;
    int m_collected;
    Vector3D m_last_pos;
  };
}
}

#endif
