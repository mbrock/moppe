#include <lavoir/wave.hh>

#include <tests/test.hh>

#include <cmath>

namespace lv = lavoir;

namespace {
  lv::water_line calm_pool () {
    return lv::water_line (lv::interval (256),
                           (12.0f / 256.0f) * mp_units::si::metre / lv::unit,
                           0.35f * mp_units::si::metre / mp_units::si::second,
                           (1.0 / 60.0) * mp_units::si::second / lv::step);
  }
}

MOPPE_TEST (lavoir_water_rejects_unstable_construction) {
  bool rejected = false;
  try {
    lv::water_line too_fast (lv::interval (256),
                             0.001f * mp_units::si::metre / lv::unit,
                             10.0f * mp_units::si::metre / mp_units::si::second,
                             (1.0 / 60.0) * mp_units::si::second / lv::step);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  MOPPE_CHECK (rejected);
}

MOPPE_TEST (lavoir_water_ages_along_its_own_axis) {
  lv::water_line pool = calm_pool ();
  MOPPE_CHECK (pool.age () == lv::steps_t::zero ());

  for (int i = 0; i < 200; ++i)
    pool.tick ();
  MOPPE_CHECK (pool.age () == 200 * lv::step);

  // A duration over the pace recovers the step count.
  const auto elapsed = pool.age () * pool.pace ();
  MOPPE_CHECK (elapsed / pool.pace () == 200 * lv::step);
}

MOPPE_TEST (lavoir_water_rains_finitely_and_deterministically) {
  lv::water_line first = calm_pool ();
  lv::water_line second = calm_pool ();
  for (int i = 0; i < 400; ++i) {
    first.tick ();
    second.tick ();
  }

  const auto surface = first.surface ();
  bool disturbed = false;
  bool finite = true;
  lv::metres_q deepest = lv::metres_q::zero ();
  for (std::size_t site = 0; site < surface.domain ().size (); ++site) {
    const lv::metres_q height = surface[site];
    disturbed = disturbed || height != lv::metres_q::zero ();
    finite =
      finite && std::isfinite (height.numerical_value_in (mp_units::si::metre));
    deepest = std::min (deepest, height);
  }
  MOPPE_CHECK (disturbed);
  MOPPE_CHECK (finite);
  MOPPE_CHECK (deepest < lv::metres_q::zero ());

  // The rain is derived from the step count: two pools with one
  // history hold one surface.
  const auto other = second.surface ();
  bool identical = true;
  for (std::size_t site = 0; site < surface.domain ().size (); ++site)
    identical = identical && surface[site] == other[site];
  MOPPE_CHECK (identical);
}
