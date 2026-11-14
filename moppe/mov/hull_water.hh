#ifndef MOPPE_MOV_HULL_WATER_HH
#define MOPPE_MOV_HULL_WATER_HH

#include <moppe/gfx/math.hh>

#include <functional>
#include <span>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  // The water a boat floats on, as the game reads it: the surface's
  // height at (x, z), below the ground where it is dry, and how the water
  // moves there, metres a second: a river's current and the waves' to and
  // fro and up and down.
  struct BoatWater {
    std::function<float (float x, float z)> level;
    std::function<Vec3 (float x, float z)> current;
  };

  // A small piece of a hull's skin, in the hull's frame: its middle, its
  // outward normal and area, and half its two edges, by which its depth
  // across it is reckoned.
  struct HullPanel {
    Vec3 centre, normal;
    float area = 0.0f;
    Vec3 half_u, half_v;
    // The transom ventilates: the water draws away from it without
    // sucking at it.
    bool transom = false;
    // An open hull floods over its gunwale, and the water inside pushes
    // on the panel as hard as the water outside: the head that buoys the
    // panel is at most the height of `rim`, the gunwale over it, above
    // it. A decked hull, panelled all round, keeps the water out.
    bool open = false;
    Vec3 rim;
  };

  // Splits the quad a, b, c, d (in order round it) into n by m panels
  // whose normals face away from `inside`.
  void add_hull_quad (std::vector<HullPanel>& out,
                      const Vec3& a,
                      const Vec3& b,
                      const Vec3& c,
                      const Vec3& d,
                      int n,
                      int m,
                      const Vec3& inside,
                      bool transom = false);

  // How the water presses on a hull: the pressure where a panel pushes
  // into the water, as a share of the stagnation pressure, and the
  // lesser suction where it draws away; a linear part of it, metres a
  // second, that damps the hull's bobbing; and the skin friction along
  // it.
  struct HullWaterTraits {
    float ram = 1.0f;
    float suction = 0.25f;
    float damping = 0.6f;
    float friction = 0.0055f;
  };

  inline constexpr float water_density = 1000.0f;
  inline constexpr float water_gravity = 9.81f;

  // The water on each of `body`'s panels, applied to it as forces: the
  // pressure at each submerged panel's depth, which floats, heels, and
  // trims the hull; the stagnation pressure where it pushes into the
  // water and the suction where it draws away, which hold it to its
  // course and lift it onto the plane; and the water's friction along
  // it, all relative to the water's own motion. Returns the wetted
  // area, square metres.
  float press_hull (b3BodyId body,
                    std::span<const HullPanel> panels,
                    const BoatWater& water,
                    const HullWaterTraits& traits);
}

#endif
