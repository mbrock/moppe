#include <moppe/game/jeep_model.hh>
#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/jeep.hh>
#include <moppe/terrain/roads.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace moppe;

namespace {
  // A 256 m square of terrain at one-metre spacing.
  map::SurfaceGeometry
  jeep_test_ground (const std::function<float (float, float)>& height) {
    map::SurfaceGeometry surface (terrain::TerrainDomain (
      256, 256, spatial_extent_in_metres (Vec3 (256, 0, 256))));
    for (std::size_t z = 0; z < surface.domain ().height (); ++z)
      for (std::size_t x = 0; x < surface.domain ().width (); ++x)
        spatial::get<terrain::surface_elevation> (
          surface[terrain::TerrainIndex { x, z }]) =
          terrain::surface_elevation_point (
            height (static_cast<float> (x), static_cast<float> (z)) *
            mp_units::si::metre);
    map::rebuild_geometry (surface);
    return surface;
  }

  void jeep_test_run (mov::Jeep& jeep, float seconds) {
    for (int i = 0; i < static_cast<int> (seconds * 120.0f); ++i)
      jeep.update (1.0f / 120.0f, -100.0f);
  }
}

MOPPE_TEST (the_jeep_model_stands_where_the_physics_has_its_wheels) {
  MOPPE_CHECK_NEAR (game::jeep_model::wheel_fl[0], mov::Jeep::track, 1e-4f);
  MOPPE_CHECK_NEAR (game::jeep_model::wheel_fl[1], 0.0f, 1e-4f);
  MOPPE_CHECK_NEAR (game::jeep_model::wheel_fl[2], mov::Jeep::axle, 1e-4f);
  MOPPE_CHECK_NEAR (game::jeep_model::wheel_rr[0], -mov::Jeep::track, 1e-4f);
  MOPPE_CHECK_NEAR (game::jeep_model::wheel_rr[2], -mov::Jeep::axle, 1e-4f);
  for (int i = 0; i < 3; ++i)
    MOPPE_CHECK_NEAR (
      game::jeep_model::seat_driver[i], mov::Jeep::seat[i], 1e-4f);
  MOPPE_CHECK_NEAR (
    game::jeep_model::seat_passenger[0], -mov::Jeep::seat[0], 1e-4f);
}

MOPPE_TEST (a_parked_jeep_settles_on_its_springs_and_stays) {
  const map::SurfaceGeometry ground =
    jeep_test_ground ([] (float, float) { return 10.0f; });
  mov::Jeep jeep (ground);
  jeep.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  jeep_test_run (jeep, 4.0f);
  MOPPE_CHECK (jeep.asleep ());
  MOPPE_CHECK (jeep.grounded ());
  // The axles stand a wheel's radius off the ground.
  MOPPE_CHECK_NEAR (
    jeep.position ()[1], 10.0f + mov::Jeep::wheel_radius, 0.03f);
  MOPPE_CHECK (jeep.up ()[1] > 0.999f);
  for (int wheel = 0; wheel < mov::Jeep::wheel_count; ++wheel)
    MOPPE_CHECK_NEAR (jeep.wheel_drop (wheel), 0.0f, 0.03f);
  MOPPE_CHECK_NEAR (jeep.position ()[0], 100.0f, 0.05f);
  MOPPE_CHECK_NEAR (jeep.position ()[2], 100.0f, 0.05f);
}

MOPPE_TEST (a_parked_jeep_holds_on_a_hillside) {
  const map::SurfaceGeometry hill =
    jeep_test_ground ([] (float, float z) { return 10.0f + 0.25f * z; });
  mov::Jeep jeep (hill);
  jeep.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  jeep_test_run (jeep, 6.0f);
  MOPPE_CHECK_NEAR (jeep.position ()[2], 100.0f, 0.6f);
  // Nose up the slope.
  MOPPE_CHECK (jeep.forward ()[1] > 0.15f);
}

MOPPE_TEST (the_jeep_drives_steers_and_stops) {
  const map::SurfaceGeometry ground =
    jeep_test_ground ([] (float, float) { return 10.0f; });
  mov::Jeep jeep (ground);
  jeep.place (Vec3 (60, 0, 60), Vec3 (0, 0, 1));
  jeep.set_parked (false);
  jeep.set_drive (1.0f);
  jeep_test_run (jeep, 4.0f);
  const float speed = length (jeep.velocity ());
  MOPPE_CHECK (jeep.position ()[2] > 80.0f);
  MOPPE_CHECK (speed > 12.0f && speed < 40.0f);
  MOPPE_CHECK_NEAR (jeep.position ()[0], 60.0f, 0.5f);
  MOPPE_CHECK (jeep.up ()[1] > 0.98f);

  // Turning right brings the nose round toward -x (x is the driver's
  // left), without rolling over.
  jeep.set_drive (0.3f);
  jeep.set_turn (1.0f);
  jeep_test_run (jeep, 2.0f);
  MOPPE_CHECK (jeep.steer () > 0.05f);
  MOPPE_CHECK (jeep.forward ()[0] < -0.3f);
  MOPPE_CHECK (jeep.up ()[1] > 0.9f);

  jeep.set_turn (0.0f);
  jeep.set_drive (0.0f);
  jeep.set_brake (true);
  jeep_test_run (jeep, 4.0f);
  MOPPE_CHECK (length (jeep.velocity ()) < 0.3f);

  // And it backs up.
  jeep.set_brake (false);
  jeep.set_drive (-1.0f);
  const Vec3 from = jeep.position ();
  const Vec3 facing = jeep.forward ();
  jeep_test_run (jeep, 2.0f);
  MOPPE_CHECK (dot (jeep.position () - from, facing) < -3.0f);
}

MOPPE_TEST (the_jeep_crosses_rough_ground_upright) {
  const map::SurfaceGeometry rough = jeep_test_ground ([] (float x, float z) {
    return 10.0f + 0.5f * std::sin (x * 0.35f) * std::cos (z * 0.27f) +
           0.04f * z;
  });
  mov::Jeep jeep (rough);
  jeep.place (Vec3 (80, 0, 40), Vec3 (0, 0, 1));
  jeep.set_parked (false);
  jeep.set_drive (0.7f);
  float lowest_up = 1.0f;
  for (int i = 0; i < 120 * 8; ++i) {
    jeep.update (1.0f / 120.0f, -100.0f);
    lowest_up = std::min (lowest_up, static_cast<float> (jeep.up ()[1]));
    MOPPE_CHECK (std::isfinite (static_cast<float> (jeep.position ()[1])));
  }
  MOPPE_CHECK (lowest_up > 0.6f);
  MOPPE_CHECK (jeep.position ()[2] > 90.0f);
}

MOPPE_TEST (a_tree_stops_the_jeep) {
  const map::SurfaceGeometry ground =
    jeep_test_ground ([] (float, float) { return 10.0f; });
  mov::TrunkField trees;
  trees.set_trunks ({ { .root = Vec3 (100, 10, 130),
                        .axis = Vec3 (0, 1, 0),
                        .height = 12.0f,
                        .radius = 0.3f } },
                    0.0f,
                    0.0f);
  trees.focus (Vec3 (100, 10, 110));
  mov::Jeep jeep (ground);
  jeep.set_trunks (&trees);
  jeep.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  jeep.set_parked (false);
  jeep.set_drive (0.5f);
  jeep_test_run (jeep, 6.0f);
  // Its nose is two metres ahead of its origin; the trunk stands at 130.
  MOPPE_CHECK (jeep.position ()[2] < 128.5f);
  MOPPE_CHECK (jeep.position ()[2] > 120.0f);
  MOPPE_CHECK (jeep.up ()[1] > 0.9f);
}

MOPPE_TEST (a_jeep_on_its_back_is_set_on_its_wheels_again) {
  const map::SurfaceGeometry ground =
    jeep_test_ground ([] (float, float) { return 10.0f; });
  mov::Jeep jeep (ground);
  jeep.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  // Turned half round about its length, and lifted clear.
  mov::Jeep::State upset = jeep.state ();
  const Vec3 centre = upset.chassis.position + Vec3 (0, 1.5f, 0);
  const std::array<float, 4> rolled { 0.0f, 0.0f, 1.0f, 0.0f };
  for (mov::RigidBodyState& wheel : upset.wheels) {
    const Vec3 arm = wheel.position - upset.chassis.position;
    wheel.position = centre + Vec3 (-arm[0], -arm[1], arm[2]);
    wheel.rotation = rolled;
  }
  upset.chassis.position = centre;
  upset.chassis.rotation = rolled;
  jeep.restore (upset);
  MOPPE_CHECK (jeep.up ()[1] < -0.9f);
  jeep_test_run (jeep, 8.0f);
  MOPPE_CHECK (jeep.up ()[1] > 0.95f);
  MOPPE_CHECK_NEAR (
    jeep.position ()[1], 10.0f + mov::Jeep::wheel_radius, 0.05f);
}

MOPPE_TEST (a_restored_jeep_stands_as_it_did) {
  const map::SurfaceGeometry ground =
    jeep_test_ground ([] (float, float) { return 10.0f; });
  mov::Jeep jeep (ground);
  jeep.place (Vec3 (100, 0, 100), Vec3 (1, 0, 1));
  jeep.set_parked (false);
  jeep.set_drive (1.0f);
  jeep_test_run (jeep, 2.0f);
  const mov::Jeep::State kept = jeep.state ();
  jeep_test_run (jeep, 1.0f);
  jeep.restore (kept);
  // The pose comes back through the body's centre of mass, so to
  // rounding rather than to the bit.
  const mov::RigidBodyState back = jeep.state ().chassis;
  MOPPE_CHECK (length (back.position - kept.chassis.position) < 1e-3f);
  for (int i = 0; i < 4; ++i)
    MOPPE_CHECK_NEAR (back.rotation[i], kept.chassis.rotation[i], 1e-4f);
  MOPPE_CHECK_NEAR (
    length (jeep.velocity ()), length (kept.chassis.linear_velocity), 0.02f);
}

// A bridge's deck carries a jeep over the river and its railings keep it
// there: steered hard into the side half way over, the jeep meets the
// railing rather than dropping off the edge into the river below.
MOPPE_TEST (the_bridge_railing_keeps_a_jeep_on_the_deck) {
  map::SurfaceGeometry valley (terrain::TerrainDomain (
    128, 128, spatial_extent_in_metres (Vec3 (320, 0, 320))));
  for (std::size_t z = 0; z < 128; ++z)
    for (std::size_t x = 0; x < 128; ++x) {
      const float m = 2.5f * static_cast<float> (z);
      spatial::get<terrain::surface_elevation> (
        valley[terrain::TerrainIndex { x, z }]) =
        terrain::surface_elevation_point (
          (m > 150.0f && m < 210.0f ? 4.0f : 10.0f) * mp_units::si::metre);
    }
  map::rebuild_geometry (valley);
  terrain::RoadNetwork network;
  terrain::Road road { .kind = terrain::Road::Kind::Road };
  for (float z = 40.0f; z <= 300.0f; z += 4.0f) {
    road.points.push_back ({ 160.0f, z });
    road.heights.push_back (10.0f);
    if (z <= 140.0f)
      road.bridges = { { static_cast<std::uint32_t> (road.points.size () - 1),
                         0 } };
    if (z <= 220.0f)
      road.bridges.front ().last =
        static_cast<std::uint32_t> (road.points.size () - 1);
  }
  network.roads = { road };
  const terrain::RoadSurface roads (network, 320.0f, 320.0f);
  mov::attach_roads (valley, &roads);
  mov::Jeep jeep (valley);
  jeep.place (Vec3 (160, 0, 120), Vec3 (0, 0, 1));
  jeep.set_parked (false);
  jeep.set_drive (0.6f);
  float lowest = 100.0f, widest = 0.0f;
  for (int i = 0; i < 120 * 10; ++i) {
    if (jeep.position ()[2] > 165.0f)
      jeep.set_turn (1.0f);
    jeep.update (1.0f / 120.0f, -100.0f);
    if (jeep.position ()[2] > 150.0f && jeep.position ()[2] < 210.0f) {
      lowest = std::min (lowest, static_cast<float> (jeep.position ()[1]));
      widest = std::max (widest, std::abs (jeep.position ()[0] - 160.0f));
    }
  }
  mov::attach_roads (valley, nullptr);
  MOPPE_CHECK (jeep.position ()[2] > 170.0f);
  MOPPE_CHECK (lowest > 10.0f + mov::Jeep::wheel_radius - 0.25f);
  MOPPE_CHECK (widest < roads.formation ().deck_half_width + 0.5f);
}
