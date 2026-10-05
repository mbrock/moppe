#ifndef MOPPE_GAME_WALKER_HH
#define MOPPE_GAME_WALKER_HH

#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/character.hh>
#include <moppe/mov/trunk_field.hh>

namespace moppe {
  namespace game {
    // One gait cycle -- a step with each foot -- covers this much ground at
    // `speed` metres per second. The walker advances its stride phase by
    // the ground actually covered over this length, and the avatar plants
    // each foot for the same distance, so the feet never skate.
    inline float stride_cycle_length (float speed) {
      return 1.3f + 0.27f * speed;
    }

    // On foot: the rider parked the bike and walks. The body is a
    // mov::Character capsule; the walker turns controls into its intent,
    // keeps the facing, and counts out the stride the avatar animates.
    class Walker {
    public:
      static constexpr float walk_speed = 3.6f; // m/s
      static constexpr float run_speed = 8.5f;
      static constexpr float wade_speed = 2.0f;
      static constexpr float eye_height = 1.62f;

      struct State {
        mov::Character::State body {};
        Vec3 heading { 0, 0, 1 };
        control_signal_t turn {};
        control_signal_t walk {};
        control_signal_t strafe {};
        bool run {};
        bool jump_request {};
        // Fraction of the current gait cycle; the left foot strikes at
        // zero and the right at one half.
        float stride_phase {};
        // Ground speed over the last step, as the feet saw it.
        speed_t stride_speed {};
      };

      Walker ();

      State state () const;
      void restore (const State& state);

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
      // A press, remembered briefly so a jump asked for just before the
      // feet land still happens.
      void jump () {
        m_jump_request = true;
      }

      void update (seconds_t dt,
                   const map::SurfaceGeometry& surface,
                   const WorldParams& world,
                   const mov::TrunkField* trunks = nullptr);

      Vec3 position () const {
        return m_body.position ();
      }
      position_t physical_position () const {
        return m_body.state ().position;
      }
      Vec3 velocity () const {
        return m_body.velocity ();
      }
      Vec3 heading () const {
        return m_heading;
      }
      bool grounded () const {
        return m_body.grounded ();
      }
      float stride_phase () const {
        return m_phase;
      }

      // How far the body sinks into its knees after a landing, metres.
      float landing_dip () const;
      // The first-person eye: head height, eased over steps, dipped by
      // landings, and bobbing gently with each footfall.
      Vec3 eye_position () const;

    private:
      mov::Character m_body;
      Vec3 m_heading;
      control_signal_t m_turn, m_walk, m_strafe;
      bool m_run;
      bool m_jump_request;
      float m_phase;
      speed_t m_stride_speed;
    };
  }
}

#endif
