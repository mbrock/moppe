#ifndef MOPPE_GAME_CAIRN_HH
#define MOPPE_GAME_CAIRN_HH

#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>
#include <moppe/terrain/domain.hh>

#include <cstdint>
#include <functional>

namespace moppe::game {
  // The trailhead's marker: a waist-high cairn of flat, faceted fieldstones
  // stacked beside the path where the trail begins, with a few loose stones
  // at its foot.  It is recorded in world space, so it is built once per
  // world and appended to the world draw list each frame.  `ground` gives
  // the terrain's elevation under a plan position; `along` is the trail's
  // way from the trailhead, made level here.
  void build_cairn (render::DrawList& list,
                    const position_t& base,
                    const direction_t& along,
                    std::uint32_t seed,
                    const terrain::ElevationField& ground);

  // One flat, faceted fieldstone of weathered granite, as the cairn is
  // built of: `centre` the middle of its bottom face, `tilt` leaning its
  // up away from the vertical (a level offset added to the unit up, so
  // dimensionless, not a direction), and `yaw` turns its facets round it.
  // `seed` shapes and colours it.
  void build_fieldstone (render::DrawList& list,
                         std::uint32_t seed,
                         const position_t& centre,
                         meters_t radius,
                         meters_t thickness,
                         radians_t yaw,
                         const Vec3& tilt);
}

#endif
