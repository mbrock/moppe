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
    velocity_t wish_velocity {};
    bool jump = false;
    bool swimming = false; // seek the whole velocity, without gravity
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
      direction_t ground_normal = unit_direction (Vec3 (0, 1, 0));
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

    static constexpr meters_t radius = 0.3f * u::m;
    static constexpr meters_t height = 1.8f * u::m;
    static constexpr meters_t step_height = 0.45f * u::m;
    // Steeper ground than this cannot be stood on: the body slides off it.
    // The share of a unit normal that points up, the cosine of the slope.
    static constexpr proportion_t walkable_normal_y =
      0.64f * proportion[one]; // ~50 degrees
    static bool walkable (const direction_t& normal) {
      return direction_value (normal)[1] >=
             walkable_normal_y.numerical_value_in (one);
    }
    static bool too_steep (const direction_t& normal) {
      return direction_value (normal)[1] <
             walkable_normal_y.numerical_value_in (one);
    }
    static constexpr metres_per_second_squared_t gravity =
      15.0f * u::m / pow<2> (u::s);
    static constexpr speed_t jump_speed = 5.4f * u::m / u::s;

    void spawn (position_t feet);
    // Arrives in the air and already moving -- off a bike, out of a
    // glider's harness -- and lands and runs it out like any other fall.
    void spawn (position_t feet, velocity_t velocity);

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

    const position_t& position () const {
      return m_state.position;
    }
    const velocity_t& velocity () const {
      return m_state.velocity;
    }
    bool grounded () const {
      return m_state.grounded;
    }

  private:
    struct Support {
      bool found = false;
      meters_t height {};
      direction_t normal = unit_direction (Vec3 (0, 1, 0));
    };

    Support probe (const position_t& feet,
                   meters_t reach_down,
                   const map::SurfaceGeometry& surface,
                   const TrunkField* obstacles) const;
    void slide (position_t& feet,
                const displacement_t& delta,
                bool grounded,
                const map::SurfaceGeometry& surface,
                const TrunkField* obstacles);

    State m_state;
    // Scratch for the planes of the current step; never part of the state.
    std::vector<MoverPlane> m_planes;
  };
}

#endif
