#ifndef MOPPE_GAME_DOOR_HH
#define MOPPE_GAME_DOOR_HH

#include <moppe/mov/vehicle.hh>

#include <cmath>

namespace moppe {
  namespace game {
    // Every building has a door in the middle of its +z wall.  This
    // is the ONE definition shared by city rendering (the drawn door
    // quad) and walker collision (the walkable aperture) -- they were
    // duplicated magic numbers in the GL build.
    struct Door {
      // Visual door quad half-width and height.
      static float draw_half_width () {
        return 1.2f;
      }
      static float height () {
        return 2.6f;
      }

      // Walkable aperture (people fit through, motorcycles do not).
      static float pass_half_width () {
        return 1.1f;
      }
      static float pass_depth () {
        return 1.8f;
      }

      static float center_x (const mov::Box& b) {
        return (b.x0 + b.x1) / 2;
      }

      static bool in_doorway (const mov::Box& b, float x, float z) {
        return std::abs (x - center_x (b)) < pass_half_width () &&
               std::abs (z - b.z1) < pass_depth ();
      }
    };
  }
}

#endif
