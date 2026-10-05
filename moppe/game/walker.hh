#ifndef MOPPE_GAME_WALKER_HH
#define MOPPE_GAME_WALKER_HH

#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

namespace moppe {
  namespace game {
    // On-foot mode: park the bike and stretch your legs.  Toggled with
    // the secret 7-5-R combo.
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
        control_signal_t strafe {};
        bool run {};
      };

      Walker ();

      State state () const {
        return { m_pos,  m_heading,  m_vy,     m_turn, m_walk,
                 m_anim, m_grounded, m_strafe, m_run };
      }

      void restore (const State& state) {
        m_pos = state.position;
        m_heading = state.heading;
        m_vy = state.vertical_velocity;
        m_turn = state.turn;
        m_walk = state.walk;
        m_anim = state.animation_distance;
        m_grounded = state.grounded;
        m_strafe = state.strafe;
        m_run = state.run;
      }

      void spawn (position_t pos, const Vec3& heading);

      void set_turn (control_signal_t t) {
        m_turn = t;
      }
      void set_walk (control_signal_t w) {
        m_walk = w;
      }
      void set_strafe (control_signal_t s) {
        m_strafe = s;
      }
      void set_run (bool run) {
        m_run = run;
      }
      // Mouse look turns the walker directly; positive is to the right.
      void turn_by (float radians);
      void jump ();

      void update (seconds_t dt,
                   const map::SurfaceGeometry& surface,
                   const WorldParams& world,
                   const mov::TrunkField* trunks = nullptr);

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
      position_t m_pos;
      Vec3 m_heading;
      velocity_component_t m_vy;
      control_signal_t m_turn, m_walk, m_strafe;
      bool m_run;
      meters_t m_anim;
      bool m_grounded;
    };
  }
}

#endif
