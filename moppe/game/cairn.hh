#ifndef MOPPE_GAME_CAIRN_HH
#define MOPPE_GAME_CAIRN_HH

#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>

#include <cstdint>
#include <functional>

namespace moppe::game {
  // The trailhead's marker: a waist-high cairn of flat, faceted fieldstones
  // stacked beside the path where the trail begins, with a few loose stones
  // at its foot.  It is recorded in world space, so it is built once per
  // world and appended to the world draw list each frame.  `ground` gives
  // the terrain height under a horizontal position; `along` is the trail's
  // direction from the trailhead.
  void build_cairn (render::DrawList& list,
                    const Vec3& base,
                    const Vec3& along,
                    std::uint32_t seed,
                    const std::function<float (float x, float z)>& ground);
}

#endif
