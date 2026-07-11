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
      struct State {
        Vector3D position {};
        Vector3D heading {};
        float vertical_velocity {};
        float turn {};
        float walk {};
        float animation_distance {};
        bool grounded {};
      };

      Walker ();

      State state () const {
        return { m_pos, m_heading, m_vy, m_turn, m_walk, m_anim, m_grounded };
      }

      void restore (const State& state) {
        m_pos = state.position;
        m_heading = state.heading;
        m_vy = state.vertical_velocity;
        m_turn = state.turn;
        m_walk = state.walk;
        m_anim = state.animation_distance;
        m_grounded = state.grounded;
      }

      void spawn (const Vector3D& pos, const Vector3D& heading);

      void set_turn (float t) {
        m_turn = t;
      }
      void set_walk (float w) {
        m_walk = w;
      }
      void jump ();

      void update (float dt,
                   const map::HeightMap& map,
                   const std::vector<mov::Box>& boxes,
                   const WorldParams& world);

      Vector3D position () const {
        return m_pos;
      }
      Vector3D heading () const {
        return m_heading;
      }

      // The walk cycle runs off distance; time only drives idle breathing.
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
