#ifndef MOPPE_GAME_WALKER_HH
#define MOPPE_GAME_WALKER_HH

#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/character.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/terrain/sea_waves.hh>
#include <moppe/terrain/water_view.hh>
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
    inline meters_t stride_cycle_length (speed_t speed) {
      constexpr speed_t still = 0.0f * (u::m / u::s);
      return 1.10f * u::m + 0.23f * u::s * speed +
             0.19f * u::s * std::max (still, speed - 3.6f * (u::m / u::s));
    }

    // Turning on the spot, each gait cycle swings the body round this
    // much: two steps of some forty-five degrees.
    inline constexpr radians_t turn_cycle_angle = 1.6f * u::rad;

    // Gait cycles a second at ground speed `speed` while the body turns at
    // `turn`. Under way the ground sets the pace; nearly still, the turn
    // steps the feet round instead.
    inline per_second_t gait_rate (speed_t speed, radians_per_second_t turn) {
      const proportion_t still =
        (1.0f -
         std::clamp ((speed / (0.9f * (u::m / u::s))).numerical_value_in (one),
                     0.0f,
                     1.0f)) *
        proportion[one];
      return speed / stride_cycle_length (speed) +
             abs (turn) / turn_cycle_angle * still.numerical_value_in (one);
    }

    // `v` turned about the vertical by `angle`, positive toward its right
    // (cross (up, v)).
    inline Vec3 yawed (const Vec3& v, radians_t angle) {
      const Vec3 side (v[2], 0.0f, -v[0]);
      return Vec3 (v[0], 0.0f, v[2]) * std::cos (radians_value (angle)) +
             side * std::sin (radians_value (angle)) + Vec3 (0.0f, v[1], 0.0f);
    }

    // The level angle from `from` round to `to`, positive to the right.
    inline radians_t yaw_between (const direction_t& from,
                                  const direction_t& to) {
      const Vec3& a = direction_value (from);
      const Vec3& b = direction_value (to);
      return std::atan2 (a[2] * b[0] - a[0] * b[2], a[0] * b[0] + a[2] * b[2]) *
             u::rad;
    }

    // The surface of the still water standing or running at a place: a
    // river's or a lake's where the painted sheet stands over the ground
    // there, otherwise the sea's. Without a sheet, the sea's. Where things
    // are placed by the water rather than float on it.
    meters_t still_water_at (const WorldParams& world,
                             const map::SurfaceGeometry& surface,
                             terrain::WaterView water,
                             const position_t& at);

    // The waves at a place at `time` (the game's total time), as the water
    // program draws them: the sea's, a lake's barely, a river's none.
    terrain::SeaWaves water_waves_at (const map::SurfaceGeometry& surface,
                                      terrain::WaterView water,
                                      const position_t& at,
                                      double time);

    // The water's surface at a place at `time`: the still water with its
    // waves on it, which is what floats, wades, and splashes.
    meters_t water_surface_at (const WorldParams& world,
                               const map::SurfaceGeometry& surface,
                               terrain::WaterView water,
                               const position_t& at,
                               double time);

    // How fast a body wades through water `depth` deep, at most:
    // unhindered at the ankles, a walk at the shins, two metres a second at
    // the knees, and hardly more than a shuffle at the chest.
    speed_t wading_speed (meters_t depth);

    // On foot: the rider parked the bike and walks. The body is a
    // mov::Character capsule; the walker turns controls into its intent,
    // keeps the facing, and counts out the stride the avatar animates.
    class Walker {
    public:
      static constexpr speed_t walk_speed = 3.6f * u::m / u::s;
      static constexpr speed_t run_speed = 8.5f * u::m / u::s;
      static constexpr speed_t wade_speed = 2.0f * u::m / u::s; // knee deep
      static constexpr meters_t eye_height = 1.62f * u::m;

      enum class Swim { none, surface, submerged };
      struct State {
        mov::Character::State body {};
        direction_t heading = unit_direction (Vec3 (0, 0, 1));
        control_signal_t turn {};
        control_signal_t walk {};
        control_signal_t strafe {};
        bool run {};
        bool jump_request {};
        // Fraction of the current gait cycle; the left foot strikes at
        // zero and the right at one half.
        proportion_t stride_phase {};
        // Ground speed over the last step, as the feet saw it.
        speed_t stride_speed {};
        // The way the body faces, which comes round after `heading`, how
        // fast it turned over the last step (positive to the right),
        // whether it is stepping round on the spot, and how far it has
        // still to come round, counted the way the look went.
        direction_t facing = unit_direction (Vec3 (0, 0, 1));
        radians_per_second_t turn_speed {};
        bool turning {};
        radians_t lag {};
        Swim swim = Swim::none;
      };

      Walker ();

      State state () const;
      void restore (const State& state);

      void spawn (position_t pos, const direction_t& heading);
      // Let go of something moving: the body keeps that motion until its
      // feet have braked it.
      void
      spawn (position_t pos, const direction_t& heading, velocity_t velocity);

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
      void
      tether (const position_t& anchor, meters_t shortest, meters_t longest) {
        m_tether = anchor;
        m_tether_shortest = shortest;
        m_tether_longest = longest;
        m_tethered = true;
      }
      // Mouse look turns the walker's heading, where it looks and goes,
      // directly; positive is to the right. The body follows at its own
      // pace (`facing`).
      void turn_by (radians_t angle);
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
                   terrain::WaterView water = nullptr,
                   double time = 0.0,
                   control_signal_t swim_lift = {});

      position_t position () const {
        return m_body.position ();
      }
      position_t physical_position () const {
        return m_body.state ().position;
      }
      velocity_t velocity () const {
        return m_body.velocity ();
      }
      // Where the walker looks and walks: the camera's way.
      direction_t heading () const {
        return m_heading;
      }
      // Where the body faces. Under way it comes round after the heading
      // at a pace a body can turn at; standing, the head and chest look
      // round first, and only a look further aside than they can twist
      // steps the feet round on the spot.
      direction_t facing () const {
        return m_facing;
      }
      radians_per_second_t turn_speed () const {
        return m_turn_speed;
      }
      bool grounded () const {
        return !swimming () && m_body.grounded ();
      }
      bool swimming () const {
        return m_swim != Swim::none;
      }
      // Swimming under the surface rather than on it.
      bool submerged () const {
        return m_swim == Swim::submerged;
      }
      proportion_t stride_phase () const {
        return m_phase;
      }

      // How far the body sinks into its knees after a landing.
      meters_t landing_dip () const;
      // The first-person eye: head height, eased over steps, dipped by
      // landings, and bobbing gently with each footfall.
      position_t eye_position () const;

    private:
      mov::Character m_body;
      direction_t m_heading;
      control_signal_t m_turn, m_walk, m_strafe;
      bool m_run;
      bool m_jump_request;
      proportion_t m_phase;
      speed_t m_stride_speed;
      direction_t m_facing;
      radians_per_second_t m_turn_speed {};
      bool m_turning = false;
      radians_t m_lag {};
      Swim m_swim = Swim::none;
      position_t m_tether;
      meters_t m_tether_shortest {};
      meters_t m_tether_longest {};
      bool m_tethered = false;
    };
  }
}

#endif
