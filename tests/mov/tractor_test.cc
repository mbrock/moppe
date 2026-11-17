#include <moppe/game/tractor_model.hh>
#include <moppe/game/trailer_model.hh>
#include <moppe/mov/tractor.hh>

#include <tests/test.hh>

#include <cmath>
#include <functional>

using namespace moppe;

namespace {
  // A 256 m square of terrain at one-metre spacing.
  map::SurfaceGeometry
  tractor_test_ground (const std::function<float (float, float)>& height) {
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

  void tractor_test_run (mov::Tractor& tractor, float seconds) {
    for (int i = 0; i < static_cast<int> (seconds * 120.0f); ++i)
      tractor.update (moppe::seconds (1.0f / 120.0f), -100.0f * u::m);
  }
}

MOPPE_TEST (the_tractor_model_stands_where_the_physics_has_its_wheels) {
  using mov::Tractor;
  namespace model = game::tractor_model;
  MOPPE_CHECK_NEAR (
    model::wheel_fl[0], meters_value (Tractor::front_track), 1e-4f);
  MOPPE_CHECK_NEAR (
    model::wheel_fl[1], meters_value (Tractor::front_radius), 1e-4f);
  MOPPE_CHECK_NEAR (
    model::wheel_fl[2], meters_value (Tractor::front_axle), 1e-4f);
  MOPPE_CHECK_NEAR (
    model::wheel_rr[0], -meters_value (Tractor::rear_track), 1e-4f);
  MOPPE_CHECK_NEAR (
    model::wheel_rr[1], meters_value (Tractor::rear_radius), 1e-4f);
  MOPPE_CHECK_NEAR (
    model::wheel_rr[2], meters_value (Tractor::rear_axle), 1e-4f);
  for (int i = 0; i < 3; ++i) {
    MOPPE_CHECK_NEAR (
      model::seat_driver[i], displacement_value (Tractor::seat)[i], 1e-4f);
    MOPPE_CHECK_NEAR (
      model::exhaust[i], displacement_value (Tractor::exhaust_mouth)[i], 1e-4f);
  }
}

MOPPE_TEST (the_trailer_model_stands_where_the_physics_has_its_wheels) {
  using mov::Tractor;
  namespace model = game::trailer_model;
  MOPPE_CHECK_NEAR (
    model::wheel_l[0], meters_value (Tractor::trailer_track), 1e-4f);
  MOPPE_CHECK_NEAR (
    model::wheel_r[0], -meters_value (Tractor::trailer_track), 1e-4f);
  MOPPE_CHECK_NEAR (
    model::wheel_l[1], meters_value (Tractor::trailer_radius), 1e-4f);
  MOPPE_CHECK_NEAR (model::wheel_l[2], 0.0f, 1e-4f);
  for (int i = 0; i < 3; ++i)
    MOPPE_CHECK_NEAR (
      model::eye[i], displacement_value (Tractor::trailer_eye)[i], 1e-4f);
}

MOPPE_TEST (a_parked_tractor_settles_on_its_tyres_and_stays) {
  const map::SurfaceGeometry ground =
    tractor_test_ground ([] (float, float) { return 10.0f; });
  mov::Tractor tractor (ground);
  tractor.place (position (Vec3 (100, 0, 100)),
                 unit_direction (Vec3 (0, 0, 1)));
  tractor_test_run (tractor, 4.0f);
  MOPPE_CHECK (tractor.asleep ());
  MOPPE_CHECK (tractor.grounded ());
  // Its origin is on the ground, and it stands level.
  MOPPE_CHECK_NEAR (position_value (tractor.position ())[1], 10.0f, 0.03f);
  MOPPE_CHECK (direction_value (tractor.up ())[1] > 0.999f);
  for (int wheel = 0; wheel < mov::Tractor::wheel_count; ++wheel)
    MOPPE_CHECK_NEAR (meters_value (tractor.wheel_drop (wheel)), 0.0f, 0.03f);
  MOPPE_CHECK_NEAR (position_value (tractor.position ())[0], 100.0f, 0.05f);
  MOPPE_CHECK_NEAR (position_value (tractor.position ())[2], 100.0f, 0.05f);
  // The trailer stands level on its springs straight behind, its eye
  // on the pin, which is 1.42 m behind the tractor's origin.
  MOPPE_CHECK_NEAR (tractor.trailer_position ()[1], 10.0f, 0.03f);
  MOPPE_CHECK (tractor.trailer_up ()[1] > 0.999f);
  MOPPE_CHECK (tractor.trailer_forward ()[2] > 0.999f);
  MOPPE_CHECK_NEAR (tractor.trailer_position ()[2], 100.0f - 3.72f, 0.05f);
  for (int wheel = 0; wheel < mov::Tractor::trailer_wheel_count; ++wheel)
    MOPPE_CHECK_NEAR (
      meters_value (tractor.trailer_wheel_drop (wheel)), 0.0f, 0.03f);
}

MOPPE_TEST (a_parked_tractor_holds_on_a_hillside) {
  const map::SurfaceGeometry hill =
    tractor_test_ground ([] (float, float z) { return 10.0f + 0.25f * z; });
  mov::Tractor tractor (hill);
  tractor.place (position (Vec3 (100, 0, 100)),
                 unit_direction (Vec3 (0, 0, 1)));
  tractor_test_run (tractor, 6.0f);
  MOPPE_CHECK_NEAR (position_value (tractor.position ())[2], 100.0f, 0.6f);
  MOPPE_CHECK (direction_value (tractor.forward ())[1] > 0.15f);
}

MOPPE_TEST (the_tractor_drives_slowly_steers_and_stops) {
  const map::SurfaceGeometry ground =
    tractor_test_ground ([] (float, float) { return 10.0f; });
  mov::Tractor tractor (ground);
  tractor.place (position (Vec3 (60, 0, 60)), unit_direction (Vec3 (0, 0, 1)));
  tractor.set_parked (false);
  tractor.set_drive (1.0f * control_signal[one]);
  tractor_test_run (tractor, 6.0f);
  const float speed = length (velocity_value (tractor.velocity ()));
  // Governed to some thirty kilometres an hour.
  MOPPE_CHECK (speed > 6.5f && speed < 9.0f);
  MOPPE_CHECK (position_value (tractor.position ())[2] > 85.0f);
  MOPPE_CHECK_NEAR (position_value (tractor.position ())[0], 60.0f, 0.5f);
  MOPPE_CHECK (direction_value (tractor.up ())[1] > 0.98f);

  // Turning right brings the nose round toward -x (x is the driver's
  // left), without rolling over.
  tractor.set_drive (0.5f * control_signal[one]);
  tractor.set_turn (1.0f * control_signal[one]);
  tractor_test_run (tractor, 3.0f);
  MOPPE_CHECK (radians_value (tractor.steer ()) > 0.05f);
  MOPPE_CHECK (direction_value (tractor.forward ())[0] < -0.3f);
  MOPPE_CHECK (direction_value (tractor.up ())[1] > 0.9f);

  // Let go of the throttle and it comes to a stop by itself.
  tractor.set_turn (0.0f * control_signal[one]);
  tractor.set_drive (0.0f * control_signal[one]);
  tractor_test_run (tractor, 4.0f);
  MOPPE_CHECK (length (velocity_value (tractor.velocity ())) < 0.3f);

  // And it backs up, slowly.
  tractor.set_drive (-1.0f * control_signal[one]);
  const Vec3 from = position_value (tractor.position ());
  const Vec3 facing = direction_value (tractor.forward ());
  tractor_test_run (tractor, 3.0f);
  const float back = dot (position_value (tractor.position ()) - from, facing);
  MOPPE_CHECK (back < -4.0f && back > -11.0f);
}

MOPPE_TEST (the_trailer_follows_round_and_will_not_fold_back) {
  const map::SurfaceGeometry ground =
    tractor_test_ground ([] (float, float) { return 10.0f; });
  mov::Tractor tractor (ground);
  tractor.place (position (Vec3 (128, 0, 60)), unit_direction (Vec3 (0, 0, 1)));
  tractor.set_parked (false);
  const auto pin = [&tractor] {
    return position_value (tractor.point (mov::Tractor::hitch_pin));
  };
  const auto eye = [&tractor] {
    return tractor.trailer_point (
      displacement_value (mov::Tractor::trailer_eye));
  };
  // Pulled along, it runs straight behind, rolling as fast.
  tractor.set_drive (1.0f * control_signal[one]);
  tractor_test_run (tractor, 5.0f);
  MOPPE_CHECK (dot (tractor.trailer_forward (),
                    direction_value (tractor.forward ())) > 0.995f);
  MOPPE_CHECK (
    dot (tractor.trailer_position () - position_value (tractor.position ()),
         direction_value (tractor.forward ())) < -3.5f);
  MOPPE_CHECK (length (eye () - pin ()) < 0.05f);
  MOPPE_CHECK (dot (velocity_value (tractor.velocity ()),
                    direction_value (tractor.forward ())) > 6.0f);

  // Round a right-hand turn it lags the tractor's heading, cutting the
  // corner, but comes round the same way.
  tractor.set_drive (0.4f * control_signal[one]);
  tractor.set_turn (1.0f * control_signal[one]);
  tractor_test_run (tractor, 2.0f);
  const float lag = std::atan2 (
    cross (tractor.trailer_forward (), direction_value (tractor.forward ()))[1],
    dot (tractor.trailer_forward (), direction_value (tractor.forward ())));
  MOPPE_CHECK (tractor.trailer_forward ()[0] < -0.05f);
  MOPPE_CHECK (lag < -0.1f);
  MOPPE_CHECK (length (eye () - pin ()) < 0.05f);
  MOPPE_CHECK (tractor.trailer_up ()[1] > 0.95f);

  // Backing up on full lock jackknifes it, until the drawbar's limit
  // holds it some sixty-five degrees off and pushes it round.
  tractor.set_drive (-1.0f * control_signal[one]);
  float widest = 0.0f;
  for (int i = 0; i < 120 * 8; ++i) {
    tractor.update (moppe::seconds (1.0f / 120.0f), -100.0f * u::m);
    const float c =
      dot (tractor.trailer_forward (), direction_value (tractor.forward ()));
    widest = std::max (widest, std::acos (std::clamp (c, -1.0f, 1.0f)));
  }
  MOPPE_CHECK (widest > 0.9f);
  MOPPE_CHECK (widest < 1.3f);
  MOPPE_CHECK (length (eye () - pin ()) < 0.1f);
  MOPPE_CHECK (direction_value (tractor.up ())[1] > 0.9f);
  MOPPE_CHECK (tractor.trailer_up ()[1] > 0.9f);
}

MOPPE_TEST (the_tractor_climbs_a_steep_slope) {
  // One in three, towing the trailer.
  const map::SurfaceGeometry hill =
    tractor_test_ground ([] (float, float z) { return 10.0f + 0.33f * z; });
  mov::Tractor tractor (hill);
  tractor.place (position (Vec3 (100, 0, 60)), unit_direction (Vec3 (0, 0, 1)));
  tractor.set_parked (false);
  tractor.set_drive (1.0f * control_signal[one]);
  tractor_test_run (tractor, 8.0f);
  // Slowly, but gathering speed, and with its nose down.
  MOPPE_CHECK (position_value (tractor.position ())[2] > 70.0f);
  MOPPE_CHECK (dot (velocity_value (tractor.velocity ()),
                    direction_value (tractor.forward ())) > 3.0f);
  MOPPE_CHECK (direction_value (tractor.up ())[1] > 0.9f);
}

MOPPE_TEST (the_tractor_crosses_rough_ground_upright) {
  const map::SurfaceGeometry rough =
    tractor_test_ground ([] (float x, float z) {
      return 10.0f + 0.5f * std::sin (x * 0.35f) * std::cos (z * 0.27f) +
             0.04f * z;
    });
  mov::Tractor tractor (rough);
  tractor.place (position (Vec3 (80, 0, 40)), unit_direction (Vec3 (0, 0, 1)));
  tractor.set_parked (false);
  tractor.set_drive (0.8f * control_signal[one]);
  float lowest_up = 1.0f;
  for (int i = 0; i < 120 * 8; ++i) {
    tractor.update (moppe::seconds (1.0f / 120.0f), -100.0f * u::m);
    lowest_up = std::min (
      lowest_up, static_cast<float> (direction_value (tractor.up ())[1]));
    MOPPE_CHECK (std::isfinite (
      static_cast<float> (position_value (tractor.position ())[1])));
  }
  MOPPE_CHECK (lowest_up > 0.6f);
  MOPPE_CHECK (position_value (tractor.position ())[2] > 75.0f);
}

MOPPE_TEST (a_tree_stops_the_tractor) {
  const map::SurfaceGeometry ground =
    tractor_test_ground ([] (float, float) { return 10.0f; });
  mov::TrunkField trees;
  trees.set_trunks ({ { .root = position (Vec3 (100, 10, 130)),
                        .axis = unit_direction (Vec3 (0, 1, 0)),
                        .height = 12.0f * u::m,
                        .radius = 0.3f * u::m } },
                    plan_extent (0.0f * u::m, 0.0f * u::m));
  trees.focus (position (Vec3 (100, 10, 110)));
  mov::Tractor tractor (ground);
  tractor.set_trunks (&trees);
  tractor.place (position (Vec3 (100, 0, 100)),
                 unit_direction (Vec3 (0, 0, 1)));
  tractor.set_parked (false);
  tractor.set_drive (0.5f * control_signal[one]);
  tractor_test_run (tractor, 8.0f);
  // Its nose is a metre and a half ahead of its origin.
  MOPPE_CHECK (position_value (tractor.position ())[2] < 128.8f);
  MOPPE_CHECK (position_value (tractor.position ())[2] > 120.0f);
  MOPPE_CHECK (direction_value (tractor.up ())[1] > 0.9f);
}

MOPPE_TEST (a_tractor_on_its_back_is_set_on_its_wheels_again) {
  const map::SurfaceGeometry ground =
    tractor_test_ground ([] (float, float) { return 10.0f; });
  mov::Tractor tractor (ground);
  tractor.place (position (Vec3 (100, 0, 100)),
                 unit_direction (Vec3 (0, 0, 1)));
  // Turned half round about its length, and lifted clear.
  mov::Tractor::State upset = tractor.state ();
  const Vec3 centre = upset.chassis.position + Vec3 (0, 2.5f, 0);
  const std::array<float, 4> rolled { 0.0f, 0.0f, 1.0f, 0.0f };
  const auto roll = [&] (mov::RigidBodyState& body) {
    const Vec3 arm = body.position - upset.chassis.position;
    body.position = centre + Vec3 (-arm[0], -arm[1], arm[2]);
    body.rotation = rolled;
  };
  for (mov::RigidBodyState& wheel : upset.wheels)
    roll (wheel);
  // The trailer over with it.
  roll (upset.trailer);
  for (mov::RigidBodyState& wheel : upset.trailer_wheels)
    roll (wheel);
  upset.chassis.position = centre;
  upset.chassis.rotation = rolled;
  tractor.restore (upset);
  MOPPE_CHECK (direction_value (tractor.up ())[1] < -0.9f);
  tractor_test_run (tractor, 8.0f);
  MOPPE_CHECK (direction_value (tractor.up ())[1] > 0.95f);
  MOPPE_CHECK_NEAR (position_value (tractor.position ())[1], 10.0f, 0.05f);
}
