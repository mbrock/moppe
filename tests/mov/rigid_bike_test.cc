#include <moppe/mov/trunk_field.hh>
#include <moppe/mov/vehicle.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace moppe;

namespace {
  // A 300 m periodic lattice at the game's own spacing of about 2.3 m.
  map::SurfaceGeometry
  rigid_test_surface (const std::function<float (float, float)>& height) {
    constexpr int samples = 128;
    constexpr float extent = 300.0f;
    map::SurfaceGeometry surface (terrain::TerrainDomain (
      samples, samples, spatial_extent_in_metres (Vec3 (extent, 0, extent))));
    const float spacing = extent / samples;
    for (int z = 0; z < samples; ++z)
      for (int x = 0; x < samples; ++x)
        spatial::get<terrain::surface_elevation> (
          surface[terrain::TerrainIndex { static_cast<std::size_t> (x),
                                          static_cast<std::size_t> (z) }]) =
          terrain::surface_elevation_point (height (x * spacing, z * spacing) *
                                            mp_units::si::metre);
    map::rebuild_geometry (surface);
    return surface;
  }

  float flat (float, float) {
    return 10.0f;
  }

  // Rolling whoops across the direction of travel, periodic over the world.
  float whoops (float x, float z) {
    constexpr float tau = 6.2831853f;
    return 10.0f + 0.6f * std::sin (tau * z / 15.0f) +
           0.3f * std::sin (tau * (x + 0.5f * z) / 37.5f);
  }

  mov::Vehicle rigid_bike (const map::SurfaceGeometry& surface,
                           const Vec3& where,
                           degrees_t heading = 0 * u::deg) {
    return mov::Vehicle (position (where),
                         heading,
                         surface,
                         2600 * u::N,
                         30 * u::kW,
                         150 * u::kg,
                         mov::BikePhysics::rigid);
  }

  const seconds_t tick = seconds (1.0f / 120.0f);

  bool same_vector (const Vec3& a, const Vec3& b) {
    return a == b;
  }
}

MOPPE_TEST (rigid_bike_settles_on_its_suspension) {
  const map::SurfaceGeometry surface = rigid_test_surface (flat);
  mov::Vehicle bike = rigid_bike (surface, Vec3 (100, 0, 100));
  MOPPE_CHECK (bike.physics () == mov::BikePhysics::rigid);

  // Drop it from half a metre: the springs take the landing, compress past
  // their static sag, and settle back to ride height.
  mov::Vehicle::State lifted = bike.state ();
  lifted.position = position (bike.position () + Vec3 (0, 0.5f, 0));
  bike.restore (lifted);
  float deepest = 0.0f;
  for (int i = 0; i < 360; ++i) {
    bike.update (tick);
    deepest = std::min (deepest, bike.rear_wheel_drop ());
  }
  MOPPE_CHECK (deepest < -0.04f);
  MOPPE_CHECK (bike.grounded ());
  MOPPE_CHECK_NEAR (
    bike.position ()[1], 10.0f + mov::RigidBike::ride_height, 0.03f);
  MOPPE_CHECK_NEAR (bike.rear_wheel_drop (), 0.0f, 0.03f);
  MOPPE_CHECK_NEAR (bike.front_wheel_drop (), 0.0f, 0.03f);
  MOPPE_CHECK (length (bike.velocity ()) < 0.05f);
  MOPPE_CHECK (bike.render_normal ()[1] > 0.99f);
}

MOPPE_TEST (rigid_bike_throttle_drives_it_forward_upright) {
  const map::SurfaceGeometry surface = rigid_test_surface (flat);
  mov::Vehicle bike = rigid_bike (surface, Vec3 (50, 0, 50));
  bike.set_thrust (1.0f);
  for (int i = 0; i < 4 * 120; ++i)
    bike.update (tick);
  const float forward = dot (bike.velocity (), bike.orientation ());
  MOPPE_CHECK (forward > 20.0f);
  MOPPE_CHECK (bike.position ()[2] > 80.0f);
  MOPPE_CHECK (bike.render_normal ()[1] > 0.95f);
  MOPPE_CHECK (bike.grounded ());

  // Braking then reverses it.
  bike.set_thrust (-1.0f);
  for (int i = 0; i < 6 * 120; ++i)
    bike.update (tick);
  MOPPE_CHECK (dot (bike.velocity (), bike.orientation ()) < -1.0f);
}

MOPPE_TEST (rigid_bike_turns_the_way_the_classic_bike_does) {
  const map::SurfaceGeometry surface = rigid_test_surface (flat);
  mov::Vehicle rigid = rigid_bike (surface, Vec3 (150, 0, 50));
  mov::Vehicle classic (position (Vec3 (150, 0, 50)),
                        0 * u::deg,
                        surface,
                        2600 * u::N,
                        30 * u::kW,
                        150 * u::kg);
  for (mov::Vehicle* bike : { &rigid, &classic }) {
    bike->set_thrust (0.6f);
    for (int i = 0; i < 120; ++i)
      bike->update (tick);
    bike->set_yaw (45 * u::deg);
    for (int i = 0; i < 120; ++i)
      bike->update (tick);
  }
  MOPPE_CHECK (rigid.orientation ()[0] < -0.2f);
  MOPPE_CHECK (classic.orientation ()[0] < -0.2f);
  // Leaning into a right turn tips the top of the bike to the right.
  const Vec3 right = cross (rigid.orientation (), Vec3 (0, 1, 0));
  MOPPE_CHECK (dot (rigid.render_normal (), right) > 0.05f);
}

MOPPE_TEST (rigid_bike_is_stopped_by_a_trunk) {
  const map::SurfaceGeometry surface = rigid_test_surface (flat);
  mov::TrunkField trunks;
  trunks.set_trunks (
    { mov::Trunk { Vec3 (60, 10, 90), Vec3 (0, 1, 0), 12.0f, 0.5f } },
    300.0f,
    300.0f);
  mov::Vehicle bike = rigid_bike (surface, Vec3 (60, 0, 50));
  bike.set_trunks (&trunks);
  bike.set_thrust (1.0f);
  float impact = 0.0f;
  float furthest = 0.0f;
  for (int i = 0; i < 6 * 120; ++i) {
    bike.update (tick);
    impact = std::max (impact, bike.pop_impact ());
    furthest = std::max (furthest, static_cast<float> (bike.position ()[2]));
  }
  // The frame capsule reaches 1.7 m ahead of the centre of mass.
  MOPPE_CHECK (furthest < 90.0f - 0.5f - 1.0f);
  MOPPE_CHECK (impact > 1.0f);
}

MOPPE_TEST (rigid_bike_replays_and_restores_deterministically) {
  const map::SurfaceGeometry surface = rigid_test_surface (whoops);
  const auto drive = [] (mov::Vehicle& bike, int from, int count) {
    for (int i = from; i < from + count; ++i) {
      const float t = i / 120.0f;
      bike.set_thrust (t < 5.0f ? 1.0f : -0.5f);
      bike.set_yaw ((60.0f * std::sin (0.9f * t)) * u::deg);
      bike.set_boost (std::fmod (t, 3.0f) < 0.4f ? 1.0f : 0.0f, 0.3f);
      bike.update (tick);
    }
  };

  mov::Vehicle first = rigid_bike (surface, Vec3 (120, 0, 40));
  mov::Vehicle second = rigid_bike (surface, Vec3 (120, 0, 40));
  drive (first, 0, 240);
  drive (second, 0, 240);
  MOPPE_CHECK (same_vector (first.position (), second.position ()));
  MOPPE_CHECK (length (first.position () - Vec3 (120, 0, 40)) > 8.0f);

  // Every restore of one checkpoint replays identically, in any vehicle
  // prepared on the same surface.
  const mov::Vehicle::State checkpoint = first.state ();
  drive (first, 240, 360);
  const Vec3 continued = first.position ();
  first.restore (checkpoint);
  drive (first, 240, 360);
  const mov::Vehicle::State replayed = first.state ();
  mov::Vehicle third = rigid_bike (surface, Vec3 (10, 0, 10));
  third.restore (checkpoint);
  drive (third, 240, 360);
  MOPPE_CHECK (same_vector (third.position (), first.position ()));
  MOPPE_CHECK (same_vector (velocity_value (third.state ().velocity),
                            velocity_value (replayed.velocity)));
  MOPPE_CHECK (third.state ().rigid.rear_wheel == replayed.rigid.rear_wheel);
  // A rebuilt world starts without the solver's warm-start impulses, so a
  // restore tracks the uninterrupted ride closely rather than bit for bit.
  MOPPE_CHECK (length (continued - first.position ()) < 1.0f);

  // An unedited checkpoint restores exactly; an edited coarse pose moves
  // the whole assemblage with it.
  third.restore (checkpoint);
  MOPPE_CHECK (third.state ().rigid.chassis == checkpoint.rigid.chassis);
  mov::Vehicle::State moved = checkpoint;
  moved.position =
    position (position_value (checkpoint.position) + Vec3 (5, 2, 0));
  moved.heading = Vec3 (1, 0, 0);
  third.restore (moved);
  MOPPE_CHECK_NEAR (third.position ()[0],
                    position_value (checkpoint.position)[0] + 5.0f,
                    1e-3f);
  MOPPE_CHECK (third.orientation ()[0] > 0.9f);
}

MOPPE_TEST (an_unridden_bike_stays_parked_on_a_slope) {
  // A ten-degree hillside, periodic over the 300 m world.
  const map::SurfaceGeometry surface = rigid_test_surface ([] (float x, float) {
    return 10.0f + 9.0f * std::sin (6.2831853f * x / 300.0f);
  });
  const auto drift = [&] (mov::BikePhysics physics, bool parked) {
    mov::Vehicle bike (position (Vec3 (0, 0, 100)),
                       90 * u::deg,
                       surface,
                       2600 * u::N,
                       30 * u::kW,
                       150 * u::kg,
                       physics);
    // Ride a moment, then step off.
    bike.set_thrust (0.5f);
    for (int i = 0; i < 60; ++i)
      bike.update (tick);
    bike.set_thrust (0.0f);
    bike.set_parked (parked);
    for (int i = 0; i < 120; ++i)
      bike.update (tick);
    const Vec3 left = bike.position ();
    for (int i = 0; i < 5 * 120; ++i)
      bike.update (tick);
    MOPPE_CHECK (bike.state ().parked == parked);
    return length (bike.position () - left);
  };
  MOPPE_CHECK (drift (mov::BikePhysics::rigid, false) > 1.0f);
  MOPPE_CHECK (drift (mov::BikePhysics::rigid, true) < 0.05f);
  MOPPE_CHECK (drift (mov::BikePhysics::classic, false) > 1.0f);
  MOPPE_CHECK (drift (mov::BikePhysics::classic, true) < 0.05f);
}
