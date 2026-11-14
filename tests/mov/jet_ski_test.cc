#include <moppe/game/jet_ski_model.hh>
#include <moppe/mov/jet_ski.hh>

#include <tests/test.hh>

#include <cmath>
#include <functional>

using namespace moppe;

namespace {
  // A 512 m square of lake bed at two-metre spacing.
  map::SurfaceGeometry
  jet_ski_test_ground (const std::function<float (float, float)>& height) {
    map::SurfaceGeometry surface (terrain::TerrainDomain (
      256, 256, spatial_extent_in_metres (Vec3 (512, 0, 512))));
    for (std::size_t z = 0; z < surface.domain ().height (); ++z)
      for (std::size_t x = 0; x < surface.domain ().width (); ++x)
        spatial::get<terrain::surface_elevation> (
          surface[terrain::TerrainIndex { x, z }]) =
          terrain::surface_elevation_point (
            height (2.0f * static_cast<float> (x),
                    2.0f * static_cast<float> (z)) *
            mp_units::si::metre);
    map::rebuild_geometry (surface);
    return surface;
  }

  mov::BoatWater jet_ski_still_water (Vec3 current = Vec3 ()) {
    return { [] (float, float) { return 0.0f; },
             [current] (float, float) { return current; } };
  }

  void jet_ski_test_run (mov::JetSki& ski,
                         float seconds,
                         const mov::BoatWater& water = jet_ski_still_water ()) {
    for (int i = 0; i < static_cast<int> (seconds * 60.0f); ++i)
      ski.update (1.0f / 60.0f, water);
  }

  Vec3 jet_ski_level (Vec3 v) {
    v[1] = 0.0f;
    return v;
  }
}

MOPPE_TEST (
  the_jet_ski_model_puts_the_pump_and_the_saddle_where_the_physics_does) {
  for (int i = 0; i < 3; ++i) {
    MOPPE_CHECK_NEAR (
      game::jet_ski_model::jet[i], mov::JetSki::nozzle[i], 1e-3f);
    MOPPE_CHECK_NEAR (
      game::jet_ski_model::intake[i], mov::JetSki::intake[i], 1e-3f);
    MOPPE_CHECK_NEAR (
      game::jet_ski_model::seat[i], mov::JetSki::saddle[i], 1e-3f);
    MOPPE_CHECK_NEAR (
      game::jet_ski_model::column[i], mov::JetSki::column[i], 1e-3f);
  }
}

MOPPE_TEST (a_jet_ski_left_on_the_water_floats_level_and_rests) {
  const map::SurfaceGeometry lake =
    jet_ski_test_ground ([] (float, float) { return -4.0f; });
  mov::JetSki ski (lake);
  // Dropped a little high, it settles.
  ski.place (Vec3 (200, 0, 200), Vec3 (0, 0, 1), 0.3f);
  jet_ski_test_run (ski, 12.0f);
  MOPPE_CHECK (ski.afloat ());
  MOPPE_CHECK (!ski.grounded ());
  MOPPE_CHECK (ski.asleep ());
  // 205 kg on a vee bottom draws a hand and a half.
  MOPPE_CHECK (ski.position ()[1] < -0.08f);
  MOPPE_CHECK (ski.position ()[1] > -0.24f);
  MOPPE_CHECK (ski.up ()[1] > 0.995f);
  MOPPE_CHECK_NEAR (ski.thrust (), 0.0f, 1e-6f);
}

MOPPE_TEST (the_jet_ski_settles_deeper_under_its_rider_and_stays_upright) {
  const map::SurfaceGeometry lake =
    jet_ski_test_ground ([] (float, float) { return -4.0f; });
  mov::JetSki ski (lake);
  ski.place (Vec3 (200, 0, 200), Vec3 (0, 0, 1), 0.0f);
  jet_ski_test_run (ski, 10.0f);
  const float empty = ski.position ()[1];
  const float empty_mass = ski.mass ();
  ski.set_parked (false);
  jet_ski_test_run (ski, 10.0f);
  MOPPE_CHECK_NEAR (ski.mass () - empty_mass, 80.0f, 1e-3f);
  MOPPE_CHECK (ski.position ()[1] < empty - 0.015f);
  MOPPE_CHECK (ski.position ()[1] > empty - 0.09f);
  // The rider's weight is high, but the hull is broad under them.
  MOPPE_CHECK (ski.up ()[1] > 0.99f);
  // Sat there with the throttle shut, it goes nowhere.
  MOPPE_CHECK (length (jet_ski_level (ski.velocity ())) < 0.1f);
  ski.set_parked (true);
  jet_ski_test_run (ski, 10.0f);
  MOPPE_CHECK_NEAR (ski.position ()[1], empty, 0.008f);
}

MOPPE_TEST (the_jet_planes_the_jet_ski_and_its_nozzle_steers_it) {
  const map::SurfaceGeometry lake =
    jet_ski_test_ground ([] (float, float) { return -4.0f; });
  mov::JetSki ski (lake);
  ski.place (Vec3 (256, 0, 30), Vec3 (0, 0, 1), 0.0f);
  ski.set_parked (false);
  jet_ski_test_run (ski, 2.0f);
  const float floating = ski.position ()[1];
  ski.set_drive (1.0f);
  float least_up = 1.0f;
  for (int i = 0; i < 20; ++i) {
    jet_ski_test_run (ski, 0.5f);
    least_up = std::min<float> (least_up, ski.up ()[1]);
  }
  // Flat out on still water it does just under 60 km/h, straight,
  // riding high on the after part of its bottom.
  const float speed = length (jet_ski_level (ski.velocity ()));
  MOPPE_CHECK (speed > 15.0f);
  MOPPE_CHECK (speed < 18.5f);
  MOPPE_CHECK (ski.forward ()[2] > 0.98f);
  MOPPE_CHECK (ski.position ()[1] > floating + 0.05f);
  MOPPE_CHECK (least_up > 0.95f);
  MOPPE_CHECK (ski.intake_depth () > 0.0f);

  // The bars over, it turns to the right, toward -x, banked into it.
  ski.set_turn (1.0f);
  for (int i = 0; i < 6; ++i) {
    jet_ski_test_run (ski, 0.5f);
    MOPPE_CHECK (ski.up ()[1] > 0.75f);
  }
  MOPPE_CHECK (ski.forward ()[0] < -0.5f);

  // Throttle shut, it sinks back off the plane and all but stops.
  ski.set_turn (0.0f);
  ski.set_drive (0.0f);
  jet_ski_test_run (ski, 12.0f);
  MOPPE_CHECK (length (jet_ski_level (ski.velocity ())) < 1.5f);
  MOPPE_CHECK (ski.up ()[1] > 0.99f);
}

MOPPE_TEST (with_the_throttle_shut_the_jet_ski_has_nothing_to_steer_with) {
  const map::SurfaceGeometry lake =
    jet_ski_test_ground ([] (float, float) { return -4.0f; });
  mov::JetSki ski (lake);
  ski.place (Vec3 (256, 0, 30), Vec3 (0, 0, 1), 0.0f);
  ski.set_parked (false);
  ski.set_drive (1.0f);
  jet_ski_test_run (ski, 5.0f);
  ski.set_drive (0.0f);
  ski.set_turn (1.0f);
  jet_ski_test_run (ski, 2.0f);
  MOPPE_CHECK_NEAR (ski.thrust (), 0.0f, 1e-6f);
  // It leans as its rider does and comes round a little, no more.
  MOPPE_CHECK (ski.forward ()[2] > 0.8f);
}

MOPPE_TEST (astern_the_jet_skis_bucket_backs_it_slowly) {
  const map::SurfaceGeometry lake =
    jet_ski_test_ground ([] (float, float) { return -4.0f; });
  mov::JetSki ski (lake);
  ski.place (Vec3 (256, 0, 100), Vec3 (0, 0, 1), 0.0f);
  ski.set_parked (false);
  ski.set_drive (-1.0f);
  jet_ski_test_run (ski, 6.0f);
  const float astern = -ski.velocity ()[2];
  MOPPE_CHECK (astern > 0.8f);
  MOPPE_CHECK (astern < 4.0f);
  MOPPE_CHECK (ski.up ()[1] > 0.98f);
}

MOPPE_TEST (a_jet_ski_thrown_off_a_wave_loses_its_push_until_it_lands) {
  const map::SurfaceGeometry lake =
    jet_ski_test_ground ([] (float, float) { return -6.0f; });
  mov::JetSki ski (lake);
  ski.place (Vec3 (256, 0, 100), Vec3 (0, 0, 1), 0.0f);
  ski.set_parked (false);
  ski.set_drive (1.0f);
  jet_ski_test_run (ski, 4.0f);
  MOPPE_CHECK (ski.thrust () > 200.0f);
  // Lifted clear of the water, its grate sucks air.
  mov::JetSki::State lifted = ski.state ();
  lifted.body.position[1] = 1.2f;
  lifted.body.linear_velocity[1] = 0.0f;
  ski.restore (lifted);
  ski.update (1.0f / 60.0f, jet_ski_still_water ());
  MOPPE_CHECK (ski.intake_depth () < 0.0f);
  MOPPE_CHECK_NEAR (ski.thrust (), 0.0f, 1e-3f);
  MOPPE_CHECK (!ski.afloat ());
  // It comes down, stays upright, and drives on.
  jet_ski_test_run (ski, 3.0f);
  MOPPE_CHECK (ski.afloat ());
  MOPPE_CHECK (ski.up ()[1] > 0.9f);
  MOPPE_CHECK (ski.thrust () > 200.0f);
}

MOPPE_TEST (a_jet_ski_rides_over_a_swell_without_burying_its_bow) {
  const map::SurfaceGeometry sea =
    jet_ski_test_ground ([] (float, float) { return -8.0f; });
  // A swell a metre from trough to crest and 24 m long, met head on.
  float time = 0.0f;
  const float k = 2.0f * 3.14159265f / 24.0f;
  const float omega = std::sqrt (9.81f * k);
  const mov::BoatWater swell { [&] (float, float z) {
                                return 0.5f * std::sin (k * z + omega * time);
                              },
                               [&] (float, float z) {
                                 const float phase = k * z + omega * time;
                                 return Vec3 (0.0f,
                                              0.5f * omega * std::cos (phase),
                                              -0.5f * omega * std::sin (phase));
                               } };
  mov::JetSki ski (sea);
  ski.place (Vec3 (256, 0, 40), Vec3 (0, 0, 1), 0.0f);
  ski.set_parked (false);
  ski.set_drive (1.0f);
  float least_up = 1.0f, lowest = 1e9f;
  for (int i = 0; i < 15 * 60; ++i) {
    time += 1.0f / 60.0f;
    ski.update (1.0f / 60.0f, swell);
    least_up = std::min<float> (least_up, ski.up ()[1]);
    const Vec3 at = ski.position ();
    lowest = std::min<float> (lowest, at[1] - swell.level (at[0], at[2]));
  }
  MOPPE_CHECK (least_up > 0.6f);
  MOPPE_CHECK (lowest > -0.6f);
  MOPPE_CHECK (ski.position ()[2] > 140.0f);
}

MOPPE_TEST (driven_at_a_shelving_shore_the_jet_ski_runs_aground) {
  // The bed rises out of the water at z = 160.
  const map::SurfaceGeometry shore =
    jet_ski_test_ground ([] (float, float z) { return (z - 160.0f) / 12.0f; });
  mov::JetSki ski (shore);
  ski.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1), 0.0f);
  ski.set_parked (false);
  ski.set_drive (1.0f);
  jet_ski_test_run (ski, 20.0f);
  MOPPE_CHECK (ski.grounded ());
  MOPPE_CHECK (!ski.afloat ());
  // Beached, its grate is dry and the jet pushes nothing.
  MOPPE_CHECK (ski.thrust () < 50.0f);
  MOPPE_CHECK (ski.position ()[2] > 155.0f);
  MOPPE_CHECK (ski.position ()[2] < 200.0f);
  MOPPE_CHECK (ski.up ()[1] > 0.8f);
}

MOPPE_TEST (a_jet_ski_knocked_over_is_righted_where_it_floats) {
  const map::SurfaceGeometry lake =
    jet_ski_test_ground ([] (float, float) { return -4.0f; });
  mov::JetSki ski (lake);
  ski.place (Vec3 (200, 0, 200), Vec3 (0, 0, 1), 0.0f);
  jet_ski_test_run (ski, 3.0f);
  // Turned turtle: a rotation of half a turn about its length.
  mov::JetSki::State over = ski.state ();
  over.body.rotation = { 0.0f, 0.0f, 1.0f, 0.0f };
  over.body.position[1] = 0.3f;
  over.asleep = false;
  ski.restore (over);
  jet_ski_test_run (ski, 1.0f);
  MOPPE_CHECK (ski.up ()[1] < 0.0f);
  // Decked all round, it floats upside down rather than sinking.
  MOPPE_CHECK (ski.position ()[1] > -0.2f);
  jet_ski_test_run (ski, 6.0f);
  MOPPE_CHECK (ski.up ()[1] > 0.99f);
}

MOPPE_TEST (a_jet_skis_checkpoint_replays_the_same_ride_each_time) {
  const map::SurfaceGeometry lake =
    jet_ski_test_ground ([] (float, float) { return -4.0f; });
  mov::JetSki ski (lake);
  ski.place (Vec3 (256, 0, 60), Vec3 (0, 0, 1), 0.0f);
  ski.set_parked (false);
  ski.set_drive (1.0f);
  ski.set_turn (0.4f);
  jet_ski_test_run (ski, 3.0f);
  const mov::JetSki::State kept = ski.state ();
  jet_ski_test_run (ski, 2.0f);
  const Vec3 uninterrupted = ski.position ();
  // The checkpoint holds the whole of it: the hull, the engine's speed,
  // the nozzle, and the rider's lean.
  ski.restore (kept);
  MOPPE_CHECK (length (ski.state ().body.position - kept.body.position) <
               1e-5f);
  MOPPE_CHECK (length (ski.velocity () - kept.body.linear_velocity) < 1e-5f);
  MOPPE_CHECK_NEAR (ski.rpm (), kept.rpm, 1e-6f);
  MOPPE_CHECK_NEAR (ski.steer (), kept.steer, 1e-6f);
  jet_ski_test_run (ski, 2.0f);
  const Vec3 first = ski.position ();
  // Another jet ski restored from it rides the same way, and both close
  // to the ride that was never interrupted.
  mov::JetSki other (lake);
  other.restore (kept);
  jet_ski_test_run (other, 2.0f);
  MOPPE_CHECK (length (other.position () - first) < 1e-4f);
  MOPPE_CHECK (length (first - uninterrupted) < 0.5f);
}
