#ifndef MOPPE_MOV_HULL_WATER_HH
#define MOPPE_MOV_HULL_WATER_HH

#include <moppe/gfx/math.hh>

#include <functional>
#include <span>
#include <vector>

#include <box3d/box3d.h>

namespace moppe::mov {
  // The water a boat floats on, as the game reads it under a point (its
  // height is not read): the surface's height there, below the ground
  // where it is dry, and how the water moves there: a river's current and
  // the waves' to and fro and up and down.
  struct BoatWater {
    std::function<meters_t (position_t at)> level;
    std::function<velocity_t (position_t at)> current;
  };

  // A small piece of a hull's skin, in the hull's frame: its middle, its
  // outward normal and area, and half its two edges, by which its depth
  // across it is reckoned.
  struct HullPanel {
    displacement_t centre {};
    direction_t normal {};
    square_meters_t area {};
    displacement_t half_u {}, half_v {};
    // The transom ventilates: the water draws away from it without
    // sucking at it.
    bool transom = false;
    // An open hull floods over its gunwale, and the water inside pushes
    // on the panel as hard as the water outside: the head that buoys the
    // panel is at most the height of `rim`, the gunwale over it, above
    // it. A decked hull, panelled all round, keeps the water out.
    bool open = false;
    displacement_t rim {};
  };

  // Splits the quad a, b, c, d (in order round it) into n by m panels
  // whose normals face away from `inside`.
  void add_hull_quad (std::vector<HullPanel>& out,
                      const displacement_t& a,
                      const displacement_t& b,
                      const displacement_t& c,
                      const displacement_t& d,
                      int n,
                      int m,
                      const displacement_t& inside,
                      bool transom = false);

  // How the water presses on a hull: the pressure where a panel pushes
  // into the water, as a share of the stagnation pressure, and the
  // lesser suction where it draws away; a linear part of it, metres a
  // second, that damps the hull's bobbing; and the skin friction along
  // it.
  struct HullWaterTraits {
    proportion_t ram = 1.0f * proportion[one];
    proportion_t suction = 0.25f * proportion[one];
    speed_t damping = 0.6f * u::m / u::s;
    drag_coefficient_t friction = 0.0055f * isq::drag_coefficient[one];
  };

  inline constexpr kilograms_per_cubic_metre_t water_density =
    1000.0f * u::kg / pow<3> (u::m);

  // The water on each of `body`'s panels, applied to it as forces: the
  // pressure at each submerged panel's depth, which floats, heels, and
  // trims the hull; the stagnation pressure where it pushes into the
  // water and the suction where it draws away, which hold it to its
  // course and lift it onto the plane; and the water's friction along
  // it, all relative to the water's own motion. Returns the wetted
  // area.
  square_meters_t press_hull (b3BodyId body,
                              std::span<const HullPanel> panels,
                              const BoatWater& water,
                              const HullWaterTraits& traits);
}

#endif
