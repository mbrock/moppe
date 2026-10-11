#ifndef MOPPE_RENDER_GROUND_DETAIL_HH
#define MOPPE_RENDER_GROUND_DETAIL_HH

#include <cstdint>
#include <vector>

namespace moppe::render {
  // The ground's detail below the terrain's own samples: one small tiling
  // RGBA8 sheet that the terrain's fragments read at several scales in
  // place of evaluating noise, so a fetch buys four lanes.
  //
  //   R    grain: four octaves of gradient noise, 8 to 64 cycles a tile
  //   G B  the slope of that relief along u and v, every octave equally
  //        steep, at a quarter of its root-mean-square a unit
  //   A    stones: a jittered cellular mosaic, each cell one flat tone
  //        between `stone_floor` and 1, rounding off over its shoulders
  //        to 0 in the seams
  //
  // Nothing in it is finer than eight texels across, so a reader may fade
  // a scale out by its footprint instead of needing mipmaps.
  struct GroundDetail {
    // Texels a side; cells a side of the stone mosaic; the least tone a
    // stone's face has.
    static constexpr int size = 512, cells = 32;
    static constexpr float stone_floor = 0.4f;
    // The slope lanes' unit: a stored 1 is this many root-mean-squares.
    static constexpr float slope_range = 4.0f;

    std::vector<std::uint8_t> pixels; // size * size * 4, rows of RGBA
  };

  GroundDetail make_ground_detail ();
}

#endif
