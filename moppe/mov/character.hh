#ifndef MOPPE_MOV_CHARACTER_HH
#define MOPPE_MOV_CHARACTER_HH

#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

#include <vector>

namespace moppe::mov {
  // What a body on foot asks of the ground this step: a horizontal velocity
  // to reach and whether jump was pressed.
  struct CharacterIntent {
    Vec3 wish_velocity;
    bool jump = false;
  };

  // A person-sized kinematic capsule moved with Box3D's mover planes. The
  // capsule floats a step's height above the feet, so pebbles, roots, and
  // low rocks pass beneath it and a ground probe sets the feet on whatever
  // supports them: the terrain heightfield or the top of a boulder. Trunks,
  // larger boulders, and terrain too steep to climb meet the capsule itself
  // and the solver slides it along them.
  class Character {
  public:
    struct State {
      position_t position {}; // the soles of the feet
      velocity_t velocity {};
      Vec3 ground_normal { 0, 1, 0 };
      bool grounded = true;
      // Walking off an edge leaves a moment in which jump still works; a
      // press just before landing is remembered until the feet touch.
      seconds_t coyote {};
      seconds_t jump_buffer {};
      seconds_t airborne_time {};
      seconds_t since_landing = 10.0f * u::s;
      speed_t landing_speed {};
      // A step up onto a rock snaps the feet; the presented body eases
      // through the difference instead of teleporting.
      meters_t step_offset {};
    };

    static constexpr float radius = 0.3f;
    static constexpr float height = 1.8f;
    static constexpr float step_height = 0.45f;
    // Steeper ground than this cannot be stood on: the body slides off it.
    static constexpr float walkable_normal_y = 0.64f; // ~50 degrees
    static constexpr float gravity = 15.0f;
    static constexpr float jump_speed = 5.4f;

    void spawn (position_t feet);

    void step (seconds_t dt,
               const CharacterIntent& intent,
               const map::SurfaceGeometry& surface,
               const TrunkField* obstacles);

    State state () const {
      return m_state;
    }
    void restore (const State& state) {
      m_state = state;
    }

    const Vec3& position () const {
      return position_value (m_state.position);
    }
    const Vec3& velocity () const {
      return velocity_value (m_state.velocity);
    }
    bool grounded () const {
      return m_state.grounded;
    }

  private:
    struct Support {
      bool found = false;
      float height = 0.0f;
      Vec3 normal { 0, 1, 0 };
    };

    Support probe (const Vec3& feet,
                   float reach_down,
                   const map::SurfaceGeometry& surface,
                   const TrunkField* obstacles) const;
    void slide (Vec3& feet,
                const Vec3& delta,
                bool grounded,
                const map::SurfaceGeometry& surface,
                const TrunkField* obstacles);

    State m_state;
    // Scratch for the planes of the current step; never part of the state.
    std::vector<MoverPlane> m_planes;
  };
}

#endif
