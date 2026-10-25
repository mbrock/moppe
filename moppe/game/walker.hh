#ifndef MOPPE_GAME_WALKER_HH
#define MOPPE_GAME_WALKER_HH

#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/character.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/terrain/sea_waves.hh>
#include <moppe/terrain/watercourse.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    // One gait cycle -- a step with each foot -- covers this much ground at
    // `speed` metres per second. The walker advances its stride phase by
    // the ground actually covered over this length, and the avatar plants
    // each foot for the same distance, so the feet never skate. A run
    // lengthens its stride faster than a walk does, so that it is not a
    // walk played quickly.
    inline float stride_cycle_length (float speed) {
      return 1.3f + 0.27f * speed + 0.14f * std::max (0.0f, speed - 3.6f);
    }

    // Turning on the spot, each gait cycle swings the body round this
    // much, radians: two steps of some forty-five degrees.
    inline constexpr float turn_cycle_angle = 1.6f;

    // Gait cycles a second at ground speed `speed` (m/s) while the body
    // turns at `turn` (rad/s). Under way the ground sets the pace; nearly
    // still, the turn steps the feet round instead.
    inline float gait_rate (float speed, float turn) {
      const float still = 1.0f - std::clamp (speed / 0.9f, 0.0f, 1.0f);
      return speed / stride_cycle_length (speed) +
             std::abs (turn) / turn_cycle_angle * still;
    }

    // `v` turned about the vertical by `angle` radians, positive toward
    // its right (cross (up, v)).
    inline Vec3 yawed (const Vec3& v, float angle) {
      const Vec3 side (v[2], 0.0f, -v[0]);
      return Vec3 (v[0], 0.0f, v[2]) * std::cos (angle) +
             side * std::sin (angle) + Vec3 (0.0f, v[1], 0.0f);
    }

    // The level angle from `from` round to `to`, positive to the right.
    inline float yaw_between (const Vec3& from, const Vec3& to) {
      const float across = from[2] * to[0] - from[0] * to[2];
      const float along = from[0] * to[0] + from[2] * to[2];
      return std::atan2 (across, along);
    }

    // The surface of the still water standing or running at a place: a
    // river's or a lake's where the painted sheet stands over the ground
    // there, otherwise the sea's. Without a sheet, the sea's. Where things
    // are placed by the water rather than float on it.
    meters_t still_water_at (const WorldParams& world,
                             const map::SurfaceGeometry& surface,
                             const terrain::WaterSheets* water,
                             const Vec3& at);

    // The waves at a place at `time` (the game's total time), as the water
    // program draws them: the sea's, a lake's barely, a river's none.
    terrain::SeaWaves water_waves_at (const map::SurfaceGeometry& surface,
                                      const terrain::WaterSheets* water,
                                      const Vec3& at,
                                      double time);

    // The water's surface at a place at `time`: the still water with its
    // waves on it, which is what floats, wades, and splashes.
    meters_t water_surface_at (const WorldParams& world,
                               const map::SurfaceGeometry& surface,
                               const terrain::WaterSheets* water,
                               const Vec3& at,
                               double time);

    // How fast a body wades through water `depth` metres deep, at most:
    // unhindered at the ankles, a walk at the shins, two metres a second at
    // the knees, and hardly more than a shuffle at the chest.
    float wading_speed (float depth);

    // On foot: the rider parked the bike and walks. The body is a
    // mov::Character capsule; the walker turns controls into its intent,
    // keeps the facing, and counts out the stride the avatar animates.
    class Walker {
    public:
      static constexpr float walk_speed = 3.6f; // m/s
      static constexpr float run_speed = 8.5f;
      static constexpr float wade_speed = 2.0f; // knee deep
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
        // The way the body faces, which comes round after `heading`, how
        // fast it turned over the last step (rad/s, positive to the
        // right), whether it is stepping round on the spot, and how far
        // it has still to come round, radians, counted the way the look
        // went.
        Vec3 facing { 0, 0, 1 };
        float turn_speed {};
        bool turning {};
        float lag {};
      };

      Walker ();

      State state () const;
      void restore (const State& state);

      void spawn (position_t pos, const Vec3& heading);
      // Let go of something moving: the body keeps that motion until its
      // feet have braked it.
      void spawn (position_t pos, const Vec3& heading, velocity_t velocity);

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
      // Holding a rope tied at `anchor`: through the next update the
      // walker cannot walk out further than `longest` from it, measured
      // level, nor in nearer than `shortest`, and is drawn back if they
      // are.
      void tether (const Vec3& anchor, float shortest, float longest) {
        m_tether = anchor;
        m_tether_shortest = shortest;
        m_tether_longest = longest;
        m_tethered = true;
      }
      // Mouse look turns the walker's heading, where it looks and goes,
      // directly; positive is to the right. The body follows at its own
      // pace (`facing`).
      void turn_by (float radians);
      // A press, remembered briefly so a jump asked for just before the
      // feet land still happens.
      void jump () {
        m_jump_request = true;
      }

      // Water slows the body by its depth at the feet and its current
      // carries the body along; a body standing on a stone above it is dry.
      void update (seconds_t dt,
                   const map::SurfaceGeometry& surface,
                   const WorldParams& world,
                   const mov::TrunkField* trunks = nullptr,
                   const terrain::WaterSheets* water = nullptr,
                   double time = 0.0);

      Vec3 position () const {
        return m_body.position ();
      }
      position_t physical_position () const {
        return m_body.state ().position;
      }
      Vec3 velocity () const {
        return m_body.velocity ();
      }
      // Where the walker looks and walks: the camera's way.
      Vec3 heading () const {
        return m_heading;
      }
      // Where the body faces. Under way it comes round after the heading
      // at a pace a body can turn at; standing, the head and chest look
      // round first, and only a look further aside than they can twist
      // steps the feet round on the spot.
      Vec3 facing () const {
        return m_facing;
      }
      float turn_speed () const {
        return m_turn_speed;
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
      Vec3 m_facing;
      float m_turn_speed = 0.0f;
      bool m_turning = false;
      float m_lag = 0.0f;
      Vec3 m_tether;
      float m_tether_shortest = 0.0f;
      float m_tether_longest = 0.0f;
      bool m_tethered = false;
    };
  }
}

#endif
