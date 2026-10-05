#include <moppe/game/boulders.hh>
#include <moppe/map/surface.hh>

#include <tests/recording_renderer.hh>
#include <tests/surface_fixture.hh>
#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace moppe;

namespace {
  constexpr std::size_t boulder_test_side = 129;
  constexpr float boulder_test_period = 640.0f;
  constexpr float boulder_test_ground = 75.0f;

  map::SurfaceGeometry boulder_test_plateau () {
    map::SurfaceGeometry surface = map::SurfaceGeometry (
      terrain::TerrainDomain (boulder_test_side,
                              boulder_test_side,
                              spatial_extent_in_metres (Vec3 (
                                boulder_test_period, 0, boulder_test_period))));
    std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                       terrain::surface_elevation_point (boulder_test_ground *
                                                         mp_units::si::metre));
    map::rebuild_geometry (surface);
    return surface;
  }

  // Lattice columns run along x.
  float boulder_test_x (const terrain::TerrainDomain& domain,
                        std::size_t offset) {
    return static_cast<float> (domain.index (offset).column) *
           domain.spacing_x ().numerical_value_in (u::m);
  }

  // A trail runs north-south through the middle of the plateau.
  float boulder_test_trail (float x) {
    return std::max (0.0f, 1.0f - std::abs (x - 320.0f) / 14.0f);
  }

  // Dry ground everywhere but a deep lake in one band of columns.
  bool boulder_test_lake (float x) {
    return x > 450.0f && x < 520.0f;
  }

  // Bilinear reconstruction ramps the water across the lattice cell at each
  // shore; past that the lake is metres deep.
  bool boulder_test_deep_lake (float x) {
    return x > 458.0f && x < 517.0f;
  }

  // The waterline band is eight metres wide, as in a generated world.
  float boulder_test_waterline (float x) {
    return std::min ({ 8.0f, std::abs (x - 450.0f), std::abs (x - 520.0f) });
  }

  terrain::WaterSheets
  boulder_test_water (const map::SurfaceGeometry& surface) {
    const terrain::TerrainDomain& domain = surface.domain ();
    terrain::WaterSheets water (domain);
    auto& level = spatial::get<terrain::surface_elevation> (water);
    for (std::size_t offset = 0; offset < domain.size (); ++offset)
      level[offset] = terrain::surface_elevation_point (
        (boulder_test_ground +
         (boulder_test_lake (boulder_test_x (domain, offset)) ? 3.0f : -1.0f)) *
        mp_units::si::metre);
    return water;
  }

  map::SurfaceReadings
  boulder_test_readings (const map::SurfaceGeometry& surface) {
    const terrain::TerrainDomain& domain = surface.domain ();
    std::vector<float> trails (domain.size ());
    for (std::size_t offset = 0; offset < domain.size (); ++offset)
      trails[offset] = boulder_test_trail (boulder_test_x (domain, offset));
    std::vector<float> shore (domain.size ());
    for (std::size_t offset = 0; offset < domain.size (); ++offset)
      shore[offset] = boulder_test_waterline (boulder_test_x (domain, offset));
    const std::vector<float> home (domain.size (), 0.0f);
    return test::complete_readings (
      surface,
      { .waterline = test::waterline_map (domain, shore),
        .use = test::trail_use_map (domain, trails, home) });
  }

  // Sea level 50 m: the plateau stands 25 m above it, so the relief decides
  // whether it is upland erratic country or lowland.
  game::BoulderPlan boulder_test_plan (const map::SurfaceGeometry& surface,
                                       const map::SurfaceReadings& readings,
                                       const terrain::WaterSheets& water,
                                       std::uint32_t seed,
                                       float relief) {
    return game::plan_boulders (
      surface, readings, water, seed, 50.0f * u::m, relief * u::m);
  }
}

MOPPE_TEST (boulder_plan_is_deterministic_for_a_seed) {
  const map::SurfaceGeometry surface = boulder_test_plateau ();
  const map::SurfaceReadings readings = boulder_test_readings (surface);
  const terrain::WaterSheets water = boulder_test_water (surface);
  const game::BoulderPlan first =
    boulder_test_plan (surface, readings, water, 0x51a7e5u, 30.0f);
  const game::BoulderPlan second =
    boulder_test_plan (surface, readings, water, 0x51a7e5u, 30.0f);
  const game::BoulderPlan other =
    boulder_test_plan (surface, readings, water, 0x0dd5eedu, 30.0f);
  MOPPE_CHECK (first.sites.size () > 100);
  MOPPE_CHECK (first.sites.size () == second.sites.size ());
  for (std::size_t i = 0; i < first.sites.size (); ++i) {
    MOPPE_CHECK (first.sites[i].seed == second.sites[i].seed);
    for (int axis = 0; axis < 3; ++axis)
      MOPPE_CHECK_NEAR (position_value (first.sites[i].centre)[axis],
                        position_value (second.sites[i].centre)[axis],
                        0.0f);
  }
  const auto different = [&] {
    if (other.sites.size () != first.sites.size ())
      return true;
    for (std::size_t i = 0; i < first.sites.size (); ++i)
      if (other.sites[i].seed != first.sites[i].seed)
        return true;
    return false;
  };
  MOPPE_CHECK (different ());
}

MOPPE_TEST (boulders_keep_off_the_trail_tread_and_out_of_deep_water) {
  const map::SurfaceGeometry surface = boulder_test_plateau ();
  const map::SurfaceReadings readings = boulder_test_readings (surface);
  const terrain::WaterSheets water = boulder_test_water (surface);
  const game::BoulderPlan plan =
    boulder_test_plan (surface, readings, water, 0x7a11u, 30.0f);
  MOPPE_CHECK (plan.sites.size () > 100);
  std::size_t beside_trail = 0;
  for (const game::BoulderSite& site : plan.sites) {
    const Vec3 centre = position_value (site.centre);
    const float radius = site.radius.numerical_value_in (u::m);
    // No part of the footprint reaches the tread, which ends 3.5 m from
    // the centreline.
    MOPPE_CHECK (std::abs (centre[0] - 320.0f) > 3.4f + 0.5f * radius);
    MOPPE_CHECK (!boulder_test_deep_lake (centre[0]));
    if (std::abs (centre[0] - 320.0f) < 14.0f)
      ++beside_trail;
  }
  // The verge beside the tread is not cleared.
  MOPPE_CHECK (beside_trail > 0);
}

MOPPE_TEST (boulders_are_sized_and_settled_into_the_ground) {
  const map::SurfaceGeometry surface = boulder_test_plateau ();
  const map::SurfaceReadings readings = boulder_test_readings (surface);
  const terrain::WaterSheets water = boulder_test_water (surface);
  const game::BoulderPlan plan =
    boulder_test_plan (surface, readings, water, 0x5123u, 30.0f);
  MOPPE_CHECK (!plan.sites.empty ());
  float largest = 0.0f;
  std::size_t small = 0;
  for (const game::BoulderSite& site : plan.sites) {
    const float radius = site.radius.numerical_value_in (u::m);
    MOPPE_CHECK (radius >= 0.05f && radius <= 3.0f);
    largest = std::max (largest, radius);
    if (radius <= 1.2f)
      ++small;
    // On level ground the body's centre stands its half-height less its
    // burial above the soil.
    const float rise = position_value (site.centre)[1] - boulder_test_ground;
    MOPPE_CHECK_NEAR (
      rise, (game::boulder_squash - game::boulder_burial) * radius, 1e-3f);
  }
  // Mostly knee-high stones, with a tail of large ones.
  MOPPE_CHECK (small * 10 > plan.sites.size () * 8);
  MOPPE_CHECK (largest > 1.2f);
}

MOPPE_TEST (boulders_follow_rockiness) {
  const map::SurfaceGeometry surface = boulder_test_plateau ();
  const map::SurfaceReadings readings = boulder_test_readings (surface);
  const terrain::WaterSheets water = boulder_test_water (surface);
  // The same plateau is high open ground in a world of 30 m relief and a
  // lowland meadow in one of 300 m: erratics belong only to the first.
  const std::size_t upland =
    boulder_test_plan (surface, readings, water, 0x2468u, 30.0f).sites.size ();
  const std::size_t lowland =
    boulder_test_plan (surface, readings, water, 0x2468u, 300.0f).sites.size ();
  MOPPE_CHECK (lowland > 0);
  MOPPE_CHECK (upland > 5 * lowland);
}

MOPPE_TEST (boulder_landscape_uploads_rocks_and_solid_colliders) {
  const map::SurfaceGeometry surface = boulder_test_plateau ();
  const map::SurfaceReadings readings = boulder_test_readings (surface);
  const terrain::WaterSheets water = boulder_test_water (surface);
  const game::BoulderPlan plan =
    boulder_test_plan (surface, readings, water, 0x1357u, 30.0f);
  test::RecordingRenderer renderer;
  game::BoulderLandscape landscape;
  landscape.rebuild (renderer, plan);
  landscape.draw (renderer);
  MOPPE_CHECK (renderer.boulder_instances.size () == plan.sites.size ());
  MOPPE_CHECK (renderer.boulder_draws == 1);
  MOPPE_CHECK (!landscape.colliders ().empty ());
  MOPPE_CHECK (landscape.colliders ().size () < plan.sites.size ());
  for (const mov::Trunk& collider : landscape.colliders ()) {
    MOPPE_CHECK (collider.radius >= 0.4f);
    MOPPE_CHECK (collider.height > collider.radius);
    // The capsule's crown stays near the top of the rock it stands for.
    const float crown = collider.root[1] + collider.height + collider.radius;
    MOPPE_CHECK (crown > boulder_test_ground);
  }
}
