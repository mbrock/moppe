#include <moppe/game/helicopter_model.hh>
#include <moppe/mov/helicopter.hh>

#include <tests/test.hh>

#include <cmath>
#include <functional>

using namespace moppe;

namespace {
  // A 512 m square of terrain at two-metre spacing.
  map::SurfaceGeometry
  helicopter_test_ground (const std::function<float (float, float)>& height) {
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

  void helicopter_test_run (mov::Helicopter& heli, float seconds) {
    for (int i = 0; i < static_cast<int> (seconds * 60.0f); ++i)
      heli.update (1.0f / 60.0f, -100.0f);
  }
}

MOPPE_TEST (the_helicopter_model_seats_the_pilot_where_the_physics_does) {
  for (int i = 0; i < 3; ++i)
    MOPPE_CHECK_NEAR (
      game::helicopter_model::seat_pilot[i], mov::Helicopter::seat[i], 1e-4f);
  MOPPE_CHECK_NEAR (game::helicopter_model::rotor_hub[0], 0.0f, 1e-4f);
  MOPPE_CHECK_NEAR (game::helicopter_model::rotor_hub[2], 0.0f, 1e-4f);
}

MOPPE_TEST (a_parked_helicopter_settles_on_its_skids_and_sleeps) {
  const map::SurfaceGeometry ground =
    helicopter_test_ground ([] (float, float) { return 10.0f; });
  mov::Helicopter heli (ground);
  heli.place (Vec3 (200, 0, 200), Vec3 (0, 0, 1));
  helicopter_test_run (heli, 4.0f);
  MOPPE_CHECK (heli.asleep ());
  MOPPE_CHECK (heli.grounded ());
  MOPPE_CHECK_NEAR (heli.position ()[1], 10.0f, 0.05f);
  MOPPE_CHECK (heli.up ()[1] > 0.999f);
}

MOPPE_TEST (the_helicopter_takes_off_flies_turns_and_lands) {
  const map::SurfaceGeometry ground =
    helicopter_test_ground ([] (float, float) { return 10.0f; });
  mov::Helicopter heli (ground);
  heli.place (Vec3 (100, 0, 100), Vec3 (0, 0, 1));
  heli.set_parked (false);

  // Sat with nothing asked, it spools up and stays put.
  helicopter_test_run (heli, 4.0f);
  MOPPE_CHECK (heli.grounded ());
  MOPPE_CHECK_NEAR (heli.rotor (), 1.0f, 1e-3f);

  // Climb, then hold the height.
  heli.set_lift (1.0f);
  helicopter_test_run (heli, 4.0f);
  MOPPE_CHECK (heli.altitude () > 15.0f);
  heli.set_lift (0.0f);
  helicopter_test_run (heli, 3.0f);
  const float held = heli.position ()[1];
  helicopter_test_run (heli, 3.0f);
  MOPPE_CHECK_NEAR (heli.position ()[1], held, 1.0f);
  MOPPE_CHECK (length (heli.velocity ()) < 1.0f);

  // Forward, nose down, toward its top speed.
  heli.set_drive (1.0f);
  helicopter_test_run (heli, 6.0f);
  MOPPE_CHECK (heli.velocity ()[2] > 25.0f);
  MOPPE_CHECK (heli.forward ()[1] < -0.1f);
  MOPPE_CHECK_NEAR (heli.position ()[1], held, 4.0f);

  // A right turn swings the heading toward -x.
  heli.set_turn (1.0f);
  helicopter_test_run (heli, 2.0f);
  MOPPE_CHECK (heli.forward ()[0] < -0.2f);
  heli.set_turn (0.0f);

  // Let go, and it comes to a hover.
  heli.set_drive (0.0f);
  helicopter_test_run (heli, 12.0f);
  Vec3 across = heli.velocity ();
  across[1] = 0.0f;
  MOPPE_CHECK (length (across) < 1.5f);
  MOPPE_CHECK (heli.up ()[1] > 0.98f);

  // Down, gently, onto the skids.
  heli.set_lift (-1.0f);
  helicopter_test_run (heli, 25.0f);
  MOPPE_CHECK (heli.grounded ());
  MOPPE_CHECK (heli.up ()[1] > 0.99f);
  heli.set_lift (0.0f);
  heli.set_parked (true);
  helicopter_test_run (heli, 15.0f);
  MOPPE_CHECK (heli.asleep ());
}

MOPPE_TEST (the_helicopter_keeps_clear_of_a_rising_hillside) {
  // Ground rising one in three ahead.
  const map::SurfaceGeometry hill =
    helicopter_test_ground ([] (float, float z) {
      return 10.0f + std::max (0.0f, (z - 120.0f) / 3.0f);
    });
  mov::Helicopter heli (hill);
  heli.place (Vec3 (256, 0, 60), Vec3 (0, 0, 1));
  heli.set_parked (false);
  helicopter_test_run (heli, 4.0f);
  heli.set_lift (1.0f);
  helicopter_test_run (heli, 1.0f);
  heli.set_lift (0.0f);
  heli.set_drive (1.0f);
  for (int i = 0; i < 60 * 10; ++i) {
    heli.update (1.0f / 60.0f, -100.0f);
    MOPPE_CHECK (heli.altitude () > 0.5f);
  }
  MOPPE_CHECK (heli.position ()[2] > 200.0f);
}
