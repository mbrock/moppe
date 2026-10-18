#include <moppe/game/caddy_model.hh>
#include <moppe/mov/caddy.hh>

#include <tests/test.hh>

#include <cmath>
#include <functional>

using namespace moppe;

namespace {
  // A 256 m square of terrain at one-metre spacing.
  map::SurfaceGeometry
  caddy_test_ground (const std::function<float (float, float)>& height) {
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

  void caddy_test_run (mov::Caddy& caddy, float seconds) {
    for (int i = 0; i < static_cast<int> (seconds * 120.0f); ++i)
      caddy.update (1.0f / 120.0f, -100.0f);
  }
}

MOPPE_TEST (the_caddy_model_stands_where_the_physics_has_its_wheels) {
  MOPPE_CHECK_NEAR (
    game::caddy_model::wheel_fl[0], mov::Caddy::front_track, 1e-4f);
  MOPPE_CHECK_NEAR (game::caddy_model::wheel_fl[1], 0.0f, 1e-4f);
  MOPPE_CHECK_NEAR (game::caddy_model::wheel_fl[2], mov::Caddy::axle, 1e-4f);
  MOPPE_CHECK_NEAR (
    game::caddy_model::wheel_rr[0], -mov::Caddy::rear_track, 1e-4f);
  MOPPE_CHECK_NEAR (game::caddy_model::wheel_rr[2], -mov::Caddy::axle, 1e-4f);
  for (int i = 0; i < 3; ++i)
    MOPPE_CHECK_NEAR (
      game::caddy_model::seat_driver[i], mov::Caddy::seat[i], 1e-4f);
  MOPPE_CHECK_NEAR (
    game::caddy_model::seat_passenger[0], -mov::Caddy::seat[0], 1e-4f);
}

MOPPE_TEST (a_parked_caddy_settles_on_its_springs_and_stays) {
  const map::SurfaceGeometry ground =
    caddy_test_ground ([] (float, float) { return 10.0f; });
  mov::Caddy caddy (ground);
  caddy.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  caddy_test_run (caddy, 4.0f);
  MOPPE_CHECK (caddy.asleep ());
  MOPPE_CHECK (caddy.grounded ());
  // The axles stand a wheel's radius off the ground.
  MOPPE_CHECK_NEAR (
    caddy.position ()[1], 10.0f + mov::Caddy::wheel_radius, 0.03f);
  MOPPE_CHECK (caddy.up ()[1] > 0.999f);
  for (int wheel = 0; wheel < mov::Caddy::wheel_count; ++wheel)
    MOPPE_CHECK_NEAR (caddy.wheel_drop (wheel), 0.0f, 0.03f);
  MOPPE_CHECK_NEAR (caddy.position ()[0], 100.0f, 0.05f);
  MOPPE_CHECK_NEAR (caddy.position ()[2], 100.0f, 0.05f);
}

MOPPE_TEST (a_parked_caddy_holds_on_a_hillside) {
  const map::SurfaceGeometry hill =
    caddy_test_ground ([] (float, float z) { return 10.0f + 0.25f * z; });
  mov::Caddy caddy (hill);
  caddy.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  caddy_test_run (caddy, 6.0f);
  MOPPE_CHECK_NEAR (caddy.position ()[2], 100.0f, 0.6f);
  // Nose up the slope.
  MOPPE_CHECK (caddy.forward ()[1] > 0.15f);
}

MOPPE_TEST (the_caddy_drives_steers_and_stops) {
  const map::SurfaceGeometry ground =
    caddy_test_ground ([] (float, float) { return 10.0f; });
  mov::Caddy caddy (ground);
  caddy.place (Vec3 (60, 0, 60), Vec3 (0, 0, 1));
  caddy.set_parked (false);
  caddy.set_drive (1.0f);
  caddy_test_run (caddy, 4.0f);
  // Seventy horsepower: a car's pull, nothing like a sports car's.
  const float speed = length (caddy.velocity ());
  MOPPE_CHECK (caddy.position ()[2] > 75.0f);
  MOPPE_CHECK (speed > 9.0f && speed < 20.0f);
  MOPPE_CHECK_NEAR (caddy.position ()[0], 60.0f, 0.5f);
  MOPPE_CHECK (caddy.up ()[1] > 0.98f);

  // Turning right brings the nose round toward -x (x is the driver's
  // left), without rolling over.
  caddy.set_drive (0.3f);
  caddy.set_turn (1.0f);
  caddy_test_run (caddy, 2.0f);
  MOPPE_CHECK (caddy.steer () > 0.05f);
  MOPPE_CHECK (caddy.forward ()[0] < -0.3f);
  MOPPE_CHECK (caddy.up ()[1] > 0.9f);

  caddy.set_turn (0.0f);
  caddy.set_drive (0.0f);
  caddy.set_brake (true);
  caddy_test_run (caddy, 4.0f);
  MOPPE_CHECK (length (caddy.velocity ()) < 0.3f);

  // And it backs up.
  caddy.set_brake (false);
  caddy.set_drive (-1.0f);
  const Vec3 from = caddy.position ();
  const Vec3 facing = caddy.forward ();
  caddy_test_run (caddy, 2.0f);
  MOPPE_CHECK (dot (caddy.position () - from, facing) < -3.0f);
}

MOPPE_TEST (the_caddy_crosses_rough_ground_upright) {
  const map::SurfaceGeometry rough = caddy_test_ground ([] (float x, float z) {
    return 10.0f + 0.5f * std::sin (x * 0.35f) * std::cos (z * 0.27f) +
           0.04f * z;
  });
  mov::Caddy caddy (rough);
  caddy.place (Vec3 (80, 0, 40), Vec3 (0, 0, 1));
  caddy.set_parked (false);
  caddy.set_drive (0.7f);
  float lowest_up = 1.0f;
  for (int i = 0; i < 120 * 8; ++i) {
    caddy.update (1.0f / 120.0f, -100.0f);
    lowest_up = std::min (lowest_up, static_cast<float> (caddy.up ()[1]));
    MOPPE_CHECK (std::isfinite (static_cast<float> (caddy.position ()[1])));
  }
  MOPPE_CHECK (lowest_up > 0.6f);
  MOPPE_CHECK (caddy.position ()[2] > 90.0f);
}

MOPPE_TEST (a_tree_stops_the_caddy) {
  const map::SurfaceGeometry ground =
    caddy_test_ground ([] (float, float) { return 10.0f; });
  mov::TrunkField trees;
  trees.set_trunks ({ { .root = Vec3 (100, 10, 130),
                        .axis = Vec3 (0, 1, 0),
                        .height = 12.0f,
                        .radius = 0.3f } },
                    0.0f,
                    0.0f);
  trees.focus (Vec3 (100, 10, 110));
  mov::Caddy caddy (ground);
  caddy.set_trunks (&trees);
  caddy.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  caddy.set_parked (false);
  caddy.set_drive (0.5f);
  caddy_test_run (caddy, 6.0f);
  // Its bumper is two metres ahead of its origin; the trunk stands at
  // 130.
  MOPPE_CHECK (caddy.position ()[2] < 128.3f);
  MOPPE_CHECK (caddy.position ()[2] > 120.0f);
  MOPPE_CHECK (caddy.up ()[1] > 0.9f);
}

MOPPE_TEST (a_caddy_on_its_back_is_set_on_its_wheels_again) {
  const map::SurfaceGeometry ground =
    caddy_test_ground ([] (float, float) { return 10.0f; });
  mov::Caddy caddy (ground);
  caddy.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  // Turned half round about its length, and lifted clear.
  mov::Caddy::State upset = caddy.state ();
  const Vec3 centre = upset.chassis.position + Vec3 (0, 1.5f, 0);
  const std::array<float, 4> rolled { 0.0f, 0.0f, 1.0f, 0.0f };
  for (mov::RigidBodyState& wheel : upset.wheels) {
    const Vec3 arm = wheel.position - upset.chassis.position;
    wheel.position = centre + Vec3 (-arm[0], -arm[1], arm[2]);
    wheel.rotation = rolled;
  }
  upset.chassis.position = centre;
  upset.chassis.rotation = rolled;
  caddy.restore (upset);
  MOPPE_CHECK (caddy.up ()[1] < -0.9f);
  caddy_test_run (caddy, 8.0f);
  MOPPE_CHECK (caddy.up ()[1] > 0.95f);
  MOPPE_CHECK_NEAR (
    caddy.position ()[1], 10.0f + mov::Caddy::wheel_radius, 0.05f);
}

MOPPE_TEST (a_restored_caddy_stands_as_it_did) {
  const map::SurfaceGeometry ground =
    caddy_test_ground ([] (float, float) { return 10.0f; });
  mov::Caddy caddy (ground);
  caddy.place (Vec3 (100, 0, 100), Vec3 (1, 0, 1));
  caddy.set_parked (false);
  caddy.set_drive (1.0f);
  caddy_test_run (caddy, 2.0f);
  const mov::Caddy::State kept = caddy.state ();
  caddy_test_run (caddy, 1.0f);
  caddy.restore (kept);
  // The pose comes back through the body's centre of mass, so to
  // rounding rather than to the bit.
  const mov::RigidBodyState back = caddy.state ().chassis;
  MOPPE_CHECK (length (back.position - kept.chassis.position) < 1e-3f);
  for (int i = 0; i < 4; ++i)
    MOPPE_CHECK_NEAR (back.rotation[i], kept.chassis.rotation[i], 1e-4f);
  MOPPE_CHECK_NEAR (
    length (caddy.velocity ()), length (kept.chassis.linear_velocity), 0.02f);
}

MOPPE_TEST (the_caddy_is_pulled_by_its_front_wheels) {
  const map::SurfaceGeometry ground =
    caddy_test_ground ([] (float, float) { return 10.0f; });
  mov::Caddy caddy (ground);
  caddy.place (Vec3 (60, 0, 60), Vec3 (0, 0, 1));
  caddy.set_parked (false);
  caddy.set_drive (1.0f);
  caddy_test_run (caddy, 1.5f);
  // The front pair carries more than the rear, over the engine, and the
  // nose squats less than the tail as it pulls away.
  const mov::Caddy::State s = caddy.state ();
  MOPPE_CHECK (s.load[0] + s.load[1] > s.load[2] + s.load[3]);
  MOPPE_CHECK (s.touching[0] && s.touching[1]);
  MOPPE_CHECK (caddy.throttle () > 0.9f);
}

MOPPE_TEST (the_caddy_runs_on_toward_its_top_speed) {
  // A long straight, four metres a sample.
  map::SurfaceGeometry road (terrain::TerrainDomain (
    512, 64, spatial_extent_in_metres (Vec3 (2048, 0, 256))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (road),
                     terrain::surface_elevation_point (10.0f *
                                                       mp_units::si::metre));
  map::rebuild_geometry (road);
  mov::Caddy caddy (road);
  caddy.place (Vec3 (30, 0, 128), Vec3 (1, 0, 0));
  caddy.set_parked (false);
  caddy.set_drive (1.0f);
  caddy_test_run (caddy, 35.0f);
  const float kmh = length (caddy.velocity ()) * 3.6f;
  MOPPE_CHECK (kmh > 110.0f && kmh < 150.0f);
  MOPPE_CHECK (caddy.up ()[1] > 0.98f);
}
