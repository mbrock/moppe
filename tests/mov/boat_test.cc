#include <moppe/game/boat_model.hh>
#include <moppe/mov/boat.hh>

#include <tests/test.hh>

#include <cmath>
#include <functional>

using namespace moppe;

namespace {
  // A 512 m square of lake bed at two-metre spacing.
  map::SurfaceGeometry
  boat_test_ground (const std::function<float (float, float)>& height) {
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

  mov::BoatWater still_water (Vec3 current = Vec3 ()) {
    return { [] (float, float) { return 0.0f; },
             [current] (float, float) { return current; } };
  }

  void boat_test_run (mov::Boat& boat,
                      float seconds,
                      const mov::BoatWater& water = still_water ()) {
    for (int i = 0; i < static_cast<int> (seconds * 60.0f); ++i)
      boat.update (1.0f / 60.0f, water);
  }

  Vec3 level (Vec3 v) {
    v[1] = 0.0f;
    return v;
  }
}

MOPPE_TEST (the_boat_model_puts_the_long_tail_where_the_physics_does) {
  for (int i = 0; i < 3; ++i) {
    MOPPE_CHECK_NEAR (game::boat_model::pivot[i], mov::Boat::pivot[i], 1e-3f);
    MOPPE_CHECK_NEAR (game::boat_model::hub[i], mov::Boat::hub[i], 1e-3f);
    MOPPE_CHECK_NEAR (
      game::boat_model::seat_helm[i], mov::Boat::helm[i], 1e-3f);
  }
}

MOPPE_TEST (a_moored_boat_floats_level_at_its_draft_and_rests) {
  const map::SurfaceGeometry lake =
    boat_test_ground ([] (float, float) { return -4.0f; });
  mov::Boat boat (lake);
  // Dropped a little high and askew, it settles.
  boat.place (Vec3 (200, 0, 200), Vec3 (0, 0, 1), 0.3f);
  boat_test_run (boat, 12.0f);
  MOPPE_CHECK (boat.afloat ());
  MOPPE_CHECK (!boat.grounded ());
  MOPPE_CHECK (boat.asleep ());
  // 240 kg on some five square metres of bottom draws a few centimetres.
  MOPPE_CHECK (boat.position ()[1] < -0.02f);
  MOPPE_CHECK (boat.position ()[1] > -0.10f);
  MOPPE_CHECK (boat.up ()[1] > 0.995f);
  // Left alone, its propeller is out of the water.
  MOPPE_CHECK (boat.propeller_depth () < 0.0f);
}

MOPPE_TEST (the_boat_settles_deeper_with_the_helmsman_aboard) {
  const map::SurfaceGeometry lake =
    boat_test_ground ([] (float, float) { return -4.0f; });
  mov::Boat boat (lake);
  boat.place (Vec3 (200, 0, 200), Vec3 (0, 0, 1), 0.0f);
  boat_test_run (boat, 10.0f);
  const float empty = boat.position ()[1];
  const float empty_mass = boat.mass ();
  // Someone steps aboard: 80 kg more, and some two centimetres deeper.
  boat.set_parked (false);
  boat_test_run (boat, 10.0f);
  const float manned = boat.position ()[1];
  MOPPE_CHECK_NEAR (boat.mass () - empty_mass, 80.0f, 1e-3f);
  MOPPE_CHECK (manned < empty - 0.01f);
  MOPPE_CHECK (manned > empty - 0.05f);
  // And steps off again: it lifts back to where it floated.
  boat.set_parked (true);
  boat_test_run (boat, 10.0f);
  MOPPE_CHECK_NEAR (boat.position ()[1], empty, 0.005f);
}

MOPPE_TEST (the_long_tail_drives_the_boat_up_onto_the_plane_and_steers_it) {
  const map::SurfaceGeometry lake =
    boat_test_ground ([] (float, float) { return -4.0f; });
  mov::Boat boat (lake);
  boat.place (Vec3 (60, 0, 40), Vec3 (0, 0, 1), 0.0f);
  boat.set_parked (false);
  boat.set_drive (1.0f);
  boat_test_run (boat, 3.0f);
  // The tail is down and the propeller bites.
  MOPPE_CHECK (boat.propeller_depth () > 0.0f);
  MOPPE_CHECK (boat.thrust () > 100.0f);
  boat_test_run (boat, 12.0f);
  const float speed = length (level (boat.velocity ()));
  MOPPE_CHECK (speed > 7.0f);
  MOPPE_CHECK (speed < 20.0f);
  MOPPE_CHECK (boat.forward ()[2] > 0.98f);
  // Up on the plane it rides higher than it floats.
  MOPPE_CHECK (boat.position ()[1] > -0.05f);
  MOPPE_CHECK (boat.up ()[1] > 0.97f);

  // Swinging the tail turns it to the right, toward -x.
  boat.set_turn (1.0f);
  boat_test_run (boat, 3.0f);
  MOPPE_CHECK (boat.forward ()[0] < -0.3f);
  MOPPE_CHECK (boat.up ()[1] > 0.9f);

  // Throttle off, it comes to a stop.
  boat.set_turn (0.0f);
  boat.set_drive (0.0f);
  boat_test_run (boat, 25.0f);
  MOPPE_CHECK (length (level (boat.velocity ())) < 0.6f);
}

MOPPE_TEST (a_river_carries_a_boat_left_on_it) {
  const map::SurfaceGeometry river =
    boat_test_ground ([] (float, float) { return -3.0f; });
  mov::Boat boat (river);
  boat.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1), 0.0f);
  const mov::BoatWater flowing = still_water (Vec3 (1.2f, 0, 0));
  boat_test_run (boat, 20.0f, flowing);
  MOPPE_CHECK_NEAR (boat.velocity ()[0], 1.2f, 0.2f);
  MOPPE_CHECK (!boat.asleep ());
}

MOPPE_TEST (driven_at_a_shelving_shore_the_boat_runs_aground) {
  // The bed rises out of the water at z = 160.
  const map::SurfaceGeometry shore =
    boat_test_ground ([] (float, float z) { return (z - 160.0f) / 12.0f; });
  mov::Boat boat (shore);
  boat.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1), 0.0f);
  boat.set_parked (false);
  boat.set_drive (1.0f);
  boat_test_run (boat, 30.0f);
  MOPPE_CHECK (boat.grounded ());
  MOPPE_CHECK (boat.position ()[2] > 150.0f);
  MOPPE_CHECK (boat.position ()[2] < 175.0f);
  MOPPE_CHECK (boat.up ()[1] > 0.8f);
}

MOPPE_TEST (driven_against_a_current_the_boat_makes_less_way_over_the_ground) {
  const map::SurfaceGeometry river =
    boat_test_ground ([] (float, float) { return -3.0f; });
  const auto speed_in = [&] (const mov::BoatWater& water) -> float {
    mov::Boat boat (river);
    boat.place (Vec3 (100, 0, 40), Vec3 (0, 0, 1), 0.0f);
    boat.set_parked (false);
    boat.set_drive (1.0f);
    boat_test_run (boat, 15.0f, water);
    const Vec3 v = boat.velocity ();
    return v[2];
  };
  const float still = speed_in (still_water ());
  // A 1.5 m/s current flowing down -z, against the bow.
  const float upstream = speed_in (still_water (Vec3 (0, 0, -1.5f)));
  MOPPE_CHECK_NEAR (still - upstream, 1.5f, 0.5f);
}
