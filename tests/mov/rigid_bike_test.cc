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
                         1800 * u::N,
                         5.2f * u::kW,
                         175 * u::kg,
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
  // The Trail 90's 5.2 kW makes some 50 km/h in four seconds.
  const float forward = dot (bike.velocity (), bike.orientation ());
  MOPPE_CHECK (forward > 12.0f);
  MOPPE_CHECK (bike.position ()[2] > 75.0f);
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
  // The frame capsule reaches 0.8 m ahead of the centre of mass.
  MOPPE_CHECK (furthest < 90.0f - 0.5f - 0.6f);
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

MOPPE_TEST (a_held_skid_brakes_the_bike_round_about_its_front_wheel) {
  const map::SurfaceGeometry surface = rigid_test_surface (flat);
  // Up to some 15 m/s heading along +z, then the bars over to the right
  // for a second, with and without the rear brake stamped on; then both
  // let go, riding on.
  struct Result {
    float heading, track, speed, slid, upright;
  };
  const auto turn = [&] (float skid, float steer) {
    mov::Vehicle bike = rigid_bike (surface, Vec3 (150, 0, 20));
    bike.set_thrust (0.6f);
    while (dot (bike.velocity (), bike.orientation ()) < 15.0f)
      bike.update (tick);
    bike.set_thrust (0.0f);
    bike.set_skid (skid);
    bike.set_yaw (steer * u::deg);
    Result r { 0, 0, 0, 0, 1 };
    for (int i = 0; i < 120; ++i) {
      bike.update (tick);
      r.slid = std::max (r.slid, bike.skid ());
    }
    const Vec3 f = bike.orientation ();
    const Vec3 v = bike.velocity ();
    r.heading = std::atan2 (-f[0], f[2]) * 57.29578f;
    r.track = std::atan2 (-v[0], v[2]) * 57.29578f;
    r.speed = length (v);
    bike.set_skid (0.0f);
    bike.set_yaw (0 * u::deg);
    bike.set_thrust (0.5f);
    for (int i = 0; i < 240; ++i) {
      bike.update (tick);
      r.upright =
        std::min (r.upright, static_cast<float> (bike.render_normal ()[1]));
    }
    MOPPE_CHECK (bike.grounded ());
    return r;
  };
  const Result rolling = turn (0.0f, 90.0f);
  const Result skidding = turn (1.0f, 90.0f);
  const Result straight = turn (1.0f, 0.0f);
  // Rolling, full lock at speed turns it a quarter round in a second;
  // skidding, the tail comes round past three quarters of a turn and the
  // bike is going most of the way back, having shed speed but kept some.
  MOPPE_CHECK (rolling.slid == 0.0f);
  MOPPE_CHECK (rolling.heading > 60.0f && rolling.heading < 120.0f);
  MOPPE_CHECK (skidding.slid > 0.95f);
  MOPPE_CHECK (std::abs (skidding.heading) > 140.0f);
  MOPPE_CHECK (std::abs (skidding.track) > 120.0f);
  MOPPE_CHECK (skidding.speed > 2.5f && skidding.speed < rolling.speed);
  // A straight skid only brakes, and nothing ends up on its side.
  MOPPE_CHECK_NEAR (straight.heading, 0.0f, 2.0f);
  MOPPE_CHECK (straight.speed < 13.0f);
  for (const Result& r : { rolling, skidding, straight })
    MOPPE_CHECK (r.upright > 0.6f);
}

MOPPE_TEST (a_mountain_bike_is_pedalled_skidded_round_and_hopped) {
  const map::SurfaceGeometry surface = rigid_test_surface (flat);
  mov::Vehicle bike (position (Vec3 (150, 0, 20)),
                     0 * u::deg,
                     surface,
                     320 * u::N,
                     750 * u::W,
                     95 * u::kg,
                     mov::BikePhysics::rigid,
                     mov::mountain_bike_rig);
  for (int i = 0; i < 240; ++i)
    bike.update (tick);
  // Standing on its own wheels, life size.
  MOPPE_CHECK (bike.grounded ());
  MOPPE_CHECK_NEAR (bike.position ()[1],
                    10.0f + mov::mountain_bike_rig.ride_height (),
                    0.02f);

  // Pedalled flat out, a cyclist's pace on the flat, the crank going
  // round; freewheeling, the crank stands still.
  bike.set_thrust (1.0f);
  float turned = 0.0f, last = radians_value (bike.crank ());
  for (int i = 0; i < 12 * 120; ++i) {
    bike.update (tick);
    const float now = radians_value (bike.crank ());
    turned += std::remainder (now - last, 6.2831853f);
    last = now;
  }
  const float flat_out = dot (bike.velocity (), bike.orientation ());
  MOPPE_CHECK (flat_out > 7.0f && flat_out < 12.0f);
  // Some eight radians a second.
  MOPPE_CHECK (turned > 12 * 6.0f && turned < 12 * 10.0f);
  bike.set_thrust (0.0f);
  const float coasting = radians_value (bike.crank ());
  for (int i = 0; i < 60; ++i)
    bike.update (tick);
  MOPPE_CHECK (radians_value (bike.crank ()) == coasting);

  // The rear brake stamped on with the bars over brings it round most of
  // the way to facing back, still moving and on its wheels.
  const Vec3 before = bike.orientation ();
  bike.set_skid (1.0f);
  bike.set_yaw (90 * u::deg);
  for (int i = 0; i < 120; ++i)
    bike.update (tick);
  MOPPE_CHECK (dot (before, bike.orientation ()) < -0.5f);
  MOPPE_CHECK (length (bike.velocity ()) > 2.0f);
  MOPPE_CHECK (bike.render_normal ()[1] > 0.8f);
  bike.set_skid (0.0f);
  bike.set_yaw (0 * u::deg);
  for (int i = 0; i < 360; ++i)
    bike.update (tick);
  MOPPE_CHECK (bike.grounded ());
  MOPPE_CHECK (bike.render_normal ()[1] > 0.98f);

  // A hop clears a log and comes down on its wheels.
  const float ground = bike.position ()[1];
  bike.hop (3.0f);
  float top = ground;
  for (int i = 0; i < 240; ++i) {
    bike.update (tick);
    top = std::max (top, static_cast<float> (bike.position ()[1]));
  }
  MOPPE_CHECK (top - ground > 0.25f && top - ground < 0.7f);
  MOPPE_CHECK (bike.grounded ());
  MOPPE_CHECK (bike.render_normal ()[1] > 0.98f);
}

namespace {
  // The pitch of a bike on flat ground, nose up.
  float pitch_of (const mov::Vehicle& bike) {
    return std::asin (std::clamp ((float)bike.orientation ()[1], -1.0f, 1.0f));
  }

  // A rider holding a wheelie on the throttle and the rear brake as a
  // person would at the keyboard, all or nothing: on the throttle while the
  // nose is low or falling, off it once it rises, and a dab of the brake if
  // it gets too high, judging by where the pitch is heading a moment on.
  struct WheelieRider {
    float last = 0.0f;
    void operator() (mov::Vehicle& bike) {
      const float pitch = pitch_of (bike);
      const float ahead = pitch + 0.4f * (pitch - last) / seconds_value (tick);
      last = pitch;
      bike.set_wheelie (1.0f);
      bike.set_thrust (ahead < 0.7f ? 1.0f : 0.0f);
      bike.set_skid (ahead > 1.1f ? 1.0f : 0.0f);
    }
  };
}

MOPPE_TEST (a_wheelie_is_popped_held_on_the_rear_wheel_and_set_down) {
  const map::SurfaceGeometry surface = rigid_test_surface (flat);
  // Flat out without leaning back, the traction control keeps the front
  // wheel down.
  {
    mov::Vehicle bike = rigid_bike (surface, Vec3 (150, 0, 20));
    bike.set_thrust (1.0f);
    float highest = 0.0f;
    for (int i = 0; i < 3 * 120; ++i) {
      bike.update (tick);
      highest = std::max (highest, pitch_of (bike));
      MOPPE_CHECK (!bike.wheeling ());
    }
    MOPPE_CHECK (highest < 0.15f);
  }

  // Leaning back from a standstill, the throttle lifts the front and the
  // bike rides on its rear wheel, pitched up, for as long as it is kept
  // there, the rider's weight back over the wheel.
  mov::Vehicle bike = rigid_bike (surface, Vec3 (150, 0, 20));
  for (int i = 0; i < 60; ++i)
    bike.update (tick);
  WheelieRider rider;
  float low = 10.0f, high = 0.0f, upright = 1.0f, lean = 0.0f;
  float metres = 0.0f;
  Vec3 last = bike.position ();
  for (int i = 0; i < 8 * 120; ++i) {
    rider (bike);
    bike.update (tick);
    MOPPE_CHECK (bike.grounded ());
    if (i < 2 * 120)
      continue;
    // Two seconds on, it has been up and settled.
    MOPPE_CHECK (bike.wheeling ());
    low = std::min (low, pitch_of (bike));
    high = std::max (high, pitch_of (bike));
    upright = std::min (upright, std::abs (dot (Vec3 (1, 0, 0),
                                                bike.render_normal ())));
    lean = std::max (lean, bike.rider_lean ());
    metres += length (bike.position () - last);
    last = bike.position ();
    if (i == 2 * 120)
      metres = 0.0f;
  }
  MOPPE_CHECK (low > 0.4f && high < 1.1f);
  // Upright in roll, not leaning on its side.
  MOPPE_CHECK (upright < 0.1f);
  MOPPE_CHECK (lean > 0.3f);
  // Some 35 km/h for six seconds.
  MOPPE_CHECK (metres > 45.0f);
  MOPPE_CHECK (bike.pop_impact () < 1.0f);

  // Sitting forward and off the throttle, the front comes back down onto
  // its wheel without a crash, and the bike rides on.
  bike.set_wheelie (0.0f);
  bike.set_thrust (0.0f);
  bike.set_skid (0.0f);
  for (int i = 0; i < 2 * 120; ++i)
    bike.update (tick);
  MOPPE_CHECK (!bike.wheeling ());
  MOPPE_CHECK (bike.grounded ());
  MOPPE_CHECK (std::abs (pitch_of (bike)) < 0.05f);
  MOPPE_CHECK (bike.pop_impact () < 7.0f);
  MOPPE_CHECK (dot (bike.velocity (), bike.orientation ()) > 5.0f);
}

MOPPE_TEST (the_rear_brake_sets_a_wheelie_down_and_full_throttle_loops_it) {
  const map::SurfaceGeometry surface = rigid_test_surface (flat);
  const auto pop_a_wheelie = [&] (mov::Vehicle& bike) {
    WheelieRider rider;
    for (int i = 0; i < 3 * 120; ++i) {
      rider (bike);
      bike.update (tick);
    }
    MOPPE_CHECK (bike.wheeling ());
  };

  // A held rear brake brings the nose down within a second, the tyre
  // rolling rather than locking into a skid.
  {
    mov::Vehicle bike = rigid_bike (surface, Vec3 (150, 0, 20));
    pop_a_wheelie (bike);
    bike.set_thrust (0.0f);
    bike.set_skid (1.0f);
    int frames = 0;
    while (bike.wheeling () && frames < 120) {
      bike.update (tick);
      MOPPE_CHECK (bike.skid () < 0.05f);
      ++frames;
    }
    MOPPE_CHECK (frames < 120);
  }

  // Off the throttle, without the brake, it takes longer to come down.
  {
    mov::Vehicle bike = rigid_bike (surface, Vec3 (150, 0, 20));
    pop_a_wheelie (bike);
    bike.set_thrust (0.0f);
    int frames = 0;
    while (bike.wheeling () && frames < 240)
      bike.update (tick), ++frames;
    MOPPE_CHECK (frames > 30);
  }

  // Held wide open, it rises past the vertical and goes over.
  {
    mov::Vehicle bike = rigid_bike (surface, Vec3 (150, 0, 20));
    pop_a_wheelie (bike);
    bike.set_thrust (1.0f);
    bike.set_skid (0.0f);
    float lowest_up = 1.0f;
    for (int i = 0; i < 3 * 120; ++i) {
      bike.update (tick);
      lowest_up =
        std::min (lowest_up, static_cast<float> (bike.render_normal ()[1]));
    }
    // On its back, lying across the rack.
    MOPPE_CHECK (lowest_up < -0.3f);
    // It lies where it fell, not righted onto its wheels facing back.
    MOPPE_CHECK (bike.render_normal ()[1] < 0.5f);
  }
}
