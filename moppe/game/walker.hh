#ifndef MOPPE_GAME_WALKER_HH
#define MOPPE_GAME_WALKER_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/mov/vehicle.hh>

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
        position_t position {};
        Vec3 heading {};
        velocity_component_t vertical_velocity {};
        control_signal_t turn {};
        control_signal_t walk {};
        meters_t animation_distance {};
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

      void spawn (position_t pos, const Vec3& heading);

      void set_turn (control_signal_t t) {
        m_turn = t;
      }
      void set_walk (control_signal_t w) {
        m_walk = w;
      }
      void jump ();

      void update (seconds_t dt,
                   const map::HeightMap& map,
                   const std::vector<mov::Box>& boxes,
                   const WorldParams& world);

      Vec3 position () const {
        return position_value (m_pos);
      }
      position_t physical_position () const {
        return m_pos;
      }
      Vec3 heading () const {
        return m_heading;
      }

    private:
      void collide (const std::vector<mov::Box>& boxes);

      position_t m_pos;
      Vec3 m_heading;
      velocity_component_t m_vy;
      control_signal_t m_turn, m_walk;
      meters_t m_anim;
      bool m_grounded;
    };
  }
}

#endif
