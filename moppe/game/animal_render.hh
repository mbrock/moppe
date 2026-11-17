#ifndef MOPPE_GAME_ANIMAL_RENDER_HH
#define MOPPE_GAME_ANIMAL_RENDER_HH

#include <moppe/game/animals.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/renderer.hh>
#include <moppe/terrain/domain.hh>

#include <cstdint>
#include <functional>
#include <span>

namespace moppe::game {
  // The animals (models/<species>.blend, from tools/figure/animals.py) as
  // baked rigid parts drawn through the renderer: the body set on the
  // ground under it, pitched with the slope; the neck swung down about
  // the withers until the muzzle reaches the grass, or up to look, and
  // turned to look aside; the head about the poll, working as it crops;
  // each leg's two parts swung through the walk, the trot, or the roe
  // deer's bound by its gait; and the tail swishing. Those within
  // `reach` of the camera and before it (at the copy of the world nearest
  // the camera, for the period given) are drawn; `shadows` lends the
  // nearest to the sun's shadow instead.
  void render_animals (render::Renderer& r,
                       std::span<const Animal> animals,
                       const terrain::ElevationField& ground,
                       const position_t& camera,
                       const direction_t& forward,
                       seconds_t time,
                       meters_t reach,
                       const plan_extent_t& period,
                       std::uint64_t motion_base);

  // The same for callers still holding floats: metres, seconds, and the
  // ground's height at (x, z).
  void render_animals (render::Renderer& r,
                       std::span<const Animal> animals,
                       const std::function<float (float, float)>& ground,
                       const Vec3& camera,
                       const Vec3& forward,
                       float time,
                       float reach,
                       float period_x,
                       float period_z,
                       std::uint64_t motion_base);

  // Where an animal's muzzle is, grazing or not, for the tests and for
  // judging the pose.
  [[nodiscard]] position_t
  animal_muzzle (const Animal& animal, const terrain::ElevationField& ground);
}

#endif
