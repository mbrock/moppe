#ifndef MOPPE_GAME_WALKER_HH
#define MOPPE_GAME_WALKER_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/render/draw.hh>

#include <vector>

namespace moppe {
namespace game {
  // On-foot mode: park the bike, stretch your legs, walk through
  // doors into buildings.  Toggled with the secret 7-5-R combo.
  // Port of main.cc's Walker; water_level now arrives through
  // WorldParams and the figure records into a DrawList.
  class Walker {
  public:
    Walker ();

    void spawn (const Vector3D& pos, const Vector3D& heading);

    void set_turn (float t) { m_turn = t; }
    void set_walk (float w) { m_walk = w; }
    void jump ();

    void update (float dt, const map::HeightMap& map,
                 const std::vector<mov::Box>& boxes,
                 const WorldParams& world);

    Vector3D position () const { return m_pos; }
    Vector3D heading () const { return m_heading; }

    // The walk cycle runs off the distance-accumulated phase, not
    // the clock; `time` is kept for call-site parity.
    void render (render::DrawList& dl, float time) const;

  private:
    void collide (const std::vector<mov::Box>& boxes);

    Vector3D m_pos, m_heading;
    float m_vy, m_turn, m_walk, m_anim;
    bool m_grounded;
  };
}
}

#endif
