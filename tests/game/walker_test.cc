#include <moppe/game/avatar.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/walker.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

#include <tests/test.hh>

#include <cmath>
#include <functional>

using namespace moppe;

namespace {
  // A 128 m square of terrain at one-metre spacing, its height given by a
  // function of the sample's metre coordinates.
  map::SurfaceGeometry
  walker_test_ground (const std::function<float (float, float)>& height) {
    map::SurfaceGeometry surface (terrain::TerrainDomain (
      128, 128, spatial_extent_in_metres (Vec3 (128, 0, 128))));
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

  game::WorldParams walker_test_world () {
    game::WorldParams world;
    world.map_size = spatial_extent_in_metres (Vec3 (128, 40, 128));
    world.water_level = -100 * u::m;
    return world;
  }

  constexpr float walker_test_step = 1.0f / 120.0f;

  void walk_for (game::Walker& walker,
                 float seconds,
                 const map::SurfaceGeometry& surface,
                 const game::WorldParams& world,
                 const mov::TrunkField* trunks = nullptr) {
    const int steps =
      static_cast<int> (std::round (seconds / walker_test_step));
    for (int i = 0; i < steps; ++i)
      walker.update (moppe::seconds (walker_test_step), surface, world, trunks);
  }
}

MOPPE_TEST (walker_walks_and_runs_on_the_ground_deterministically) {
  const map::SurfaceGeometry surface =
    walker_test_ground ([] (float x, float) { return 10.0f + 0.2f * x; });
  const game::WorldParams world = walker_test_world ();

  const auto run = [&] (bool sprint) {
    game::Walker walker;
    walker.spawn (position (Vec3 (20, 15, 64)), Vec3 (1, 0, 0));
    walker.set_walk (1.0f);
    walker.set_run (sprint);
    walk_for (walker, 2.0f, surface, world);
    return walker.state ();
  };
  const game::Walker::State walked = run (false);
  const game::Walker::State again = run (false);
  const game::Walker::State ran = run (true);

  // Feet on the slope, at a pace a little under level speed uphill.
  const Vec3 feet = position_value (walked.body.position);
  MOPPE_CHECK (walked.body.grounded);
  MOPPE_CHECK_NEAR (feet[1], 10.0f + 0.2f * feet[0], 0.02f);
  const float pace = walked.stride_speed.numerical_value_in (u::m / u::s);
  MOPPE_CHECK (pace > 0.8f * game::Walker::walk_speed);
  MOPPE_CHECK (pace < game::Walker::walk_speed + 0.01f);
  MOPPE_CHECK (ran.stride_speed > 2.0f * walked.stride_speed);

  // The same inputs on the same ground give bit-identical bodies.
  const Vec3 repeat = position_value (again.body.position);
  MOPPE_CHECK (repeat[0] == feet[0] && repeat[1] == feet[1] &&
               repeat[2] == feet[2]);
  MOPPE_CHECK (again.stride_phase == walked.stride_phase);
}

MOPPE_TEST (walker_slides_around_a_trunk_instead_of_sticking) {
  const map::SurfaceGeometry surface =
    walker_test_ground ([] (float, float) { return 5.0f; });
  const game::WorldParams world = walker_test_world ();
  mov::TrunkField trunks;
  trunks.set_trunks (
    { { Vec3 (30, 5, 64), Vec3 (0, 1, 0), 10.0f, 0.4f } }, 0.0f, 0.0f);
  trunks.focus (Vec3 (30, 5, 64));

  // Straight at it: stopped at the bark, never inside it.
  game::Walker blocked;
  blocked.spawn (position (Vec3 (24, 5, 64)), Vec3 (1, 0, 0));
  blocked.set_walk (1.0f);
  walk_for (blocked, 3.0f, surface, world, &trunks);
  const Vec3 stopped = blocked.position ();
  MOPPE_CHECK (stopped[0] < 30.0f - 0.4f - mov::Character::radius + 0.01f);
  MOPPE_CHECK (stopped[0] > 30.0f - 0.4f - mov::Character::radius - 0.1f);
  MOPPE_CHECK_NEAR (stopped[1], 5.0f, 1e-4f);
  // Pressed against the trunk the legs stop too.
  MOPPE_CHECK (blocked.state ().stride_speed < 0.2f * u::m / u::s);

  // A glancing line slides around the trunk and carries on past it.
  game::Walker glancing;
  glancing.spawn (position (Vec3 (24, 5, 64.3f)), Vec3 (1, 0, 0));
  glancing.set_walk (1.0f);
  walk_for (glancing, 3.0f, surface, world, &trunks);
  const Vec3 past = glancing.position ();
  MOPPE_CHECK (past[0] > 32.0f);
  MOPPE_CHECK (std::abs (past[2] - 64.0f) >
               0.4f + mov::Character::radius - 0.01f);
}

MOPPE_TEST (walker_jump_arc_lands_and_coyote_time_forgives_a_late_press) {
  const map::SurfaceGeometry surface =
    walker_test_ground ([] (float, float) { return 5.0f; });
  const game::WorldParams world = walker_test_world ();

  game::Walker walker;
  walker.spawn (position (Vec3 (40, 5, 64)), Vec3 (1, 0, 0));
  walk_for (walker, 0.1f, surface, world);
  walker.jump ();
  float apex = 0.0f;
  int airborne_steps = 0;
  for (int i = 0; i < 240; ++i) {
    walker.update (seconds (walker_test_step), surface, world);
    apex = std::max<float> (apex, walker.position ()[1] - 5.0f);
    if (!walker.grounded ())
      ++airborne_steps;
  }
  const float g = mov::Character::gravity;
  const float v = mov::Character::jump_speed;
  MOPPE_CHECK_NEAR (apex, v * v / (2.0f * g), 0.03f);
  MOPPE_CHECK_NEAR (airborne_steps * walker_test_step, 2.0f * v / g, 0.03f);
  MOPPE_CHECK (walker.grounded ());
  MOPPE_CHECK_NEAR (walker.position ()[1], 5.0f, 1e-4f);
  MOPPE_CHECK_NEAR (
    walker.state ().body.landing_speed.numerical_value_in (u::m / u::s),
    v,
    0.15f);

  // Run off a two-metre ledge and press jump a moment too late: the jump
  // still comes, because the ground was there an instant ago.
  const map::SurfaceGeometry ledge = walker_test_ground (
    [] (float x, float) { return x < 50.0f ? 7.0f : 5.0f; });
  game::Walker late;
  late.spawn (position (Vec3 (46, 7, 64)), Vec3 (1, 0, 0));
  late.set_walk (1.0f);
  int guard = 0;
  while (late.grounded () && guard++ < 600)
    late.update (seconds (walker_test_step), ledge, world);
  MOPPE_CHECK (!late.grounded ());
  walk_for (late, 0.06f, ledge, world);
  late.jump ();
  late.update (seconds (walker_test_step), ledge, world);
  MOPPE_CHECK (late.velocity ()[1] > 0.9f * v);
}

MOPPE_TEST (walker_steps_onto_low_rocks_and_is_stopped_by_tall_ones) {
  const map::SurfaceGeometry surface =
    walker_test_ground ([] (float, float) { return 5.0f; });
  const game::WorldParams world = walker_test_world ();
  // Buried vertical capsules like the boulder colliders: one whose top is
  // a shin high, one at chest height.
  const auto rock = [] (float x, float top, float radius) {
    return mov::Trunk {
      Vec3 (x, top - 2.0f - radius, 64), Vec3 (0, 1, 0), 2.0f, radius
    };
  };
  mov::TrunkField rocks;
  rocks.set_trunks (
    { rock (30, 5.3f, 0.8f), rock (40, 6.3f, 0.8f) }, 0.0f, 0.0f);
  rocks.focus (Vec3 (35, 5, 64));

  game::Walker walker;
  walker.spawn (position (Vec3 (26, 5, 64)), Vec3 (1, 0, 0));
  walker.set_walk (1.0f);
  float highest = 0.0f;
  for (int i = 0; i < 600; ++i) {
    walker.update (seconds (walker_test_step), surface, world, &rocks);
    highest = std::max<float> (highest, walker.position ()[1]);
  }
  // Up and over the low rock, then held at the tall one's flank.
  MOPPE_CHECK_NEAR (highest, 5.3f, 0.05f);
  MOPPE_CHECK (walker.position ()[0] > 31.0f);
  MOPPE_CHECK (walker.position ()[0] < 40.0f - 0.8f);
  MOPPE_CHECK_NEAR (walker.position ()[1], 5.0f, 1e-3f);
}

MOPPE_TEST (walker_cannot_climb_a_bank_too_steep_to_stand_on) {
  // A 70-degree bank rising from x = 40.
  const map::SurfaceGeometry surface = walker_test_ground (
    [] (float x, float) { return 5.0f + std::max (0.0f, x - 40.0f) * 2.75f; });
  const game::WorldParams world = walker_test_world ();
  game::Walker walker;
  walker.spawn (position (Vec3 (35, 5, 64)), Vec3 (1, 0, 0));
  walker.set_walk (1.0f);
  walk_for (walker, 4.0f, surface, world);
  MOPPE_CHECK (walker.position ()[1] < 5.0f + mov::Character::step_height);
  MOPPE_CHECK (walker.position ()[0] < 40.5f);
}

MOPPE_TEST (walker_checkpoint_resumes_the_same_walk) {
  const map::SurfaceGeometry surface =
    walker_test_ground ([] (float x, float z) {
      return 8.0f + 1.5f * std::sin (x * 0.2f) * std::cos (z * 0.15f);
    });
  const game::WorldParams world = walker_test_world ();
  mov::TrunkField trunks;
  trunks.set_trunks ({ { Vec3 (52, 0, 61), Vec3 (0, 1, 0), 20.0f, 0.35f },
                       { Vec3 (60, 0, 63.5f), Vec3 (0, 1, 0), 20.0f, 0.5f } },
                     0.0f,
                     0.0f);
  trunks.focus (Vec3 (55, 8, 62));

  const auto drive = [&] (game::Walker& walker, int from, int to) {
    for (int i = from; i < to; ++i) {
      walker.set_walk (1.0f);
      walker.set_run (i > 120);
      walker.set_strafe (i % 200 < 100 ? 0.3f : -0.2f);
      walker.turn_by (i % 90 < 45 ? 0.004f : -0.003f);
      if (i % 150 == 30)
        walker.jump ();
      walker.update (seconds (walker_test_step), surface, world, &trunks);
    }
  };

  game::Walker live;
  live.spawn (position (Vec3 (45, 12, 62)), Vec3 (1, 0, 0.1f));
  drive (live, 0, 200);
  const game::Walker::State checkpoint = live.state ();
  drive (live, 200, 600);

  game::Walker resumed;
  resumed.spawn (position (Vec3 (0, 0, 0)), Vec3 (0, 0, 1));
  resumed.restore (checkpoint);
  drive (resumed, 200, 600);

  const Vec3 a = live.position (), b = resumed.position ();
  MOPPE_CHECK (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]);
  const Vec3 va = live.velocity (), vb = resumed.velocity ();
  MOPPE_CHECK (va[0] == vb[0] && va[1] == vb[1] && va[2] == vb[2]);
  MOPPE_CHECK (live.stride_phase () == resumed.stride_phase ());
  MOPPE_CHECK (live.grounded () == resumed.grounded ());
  // The walk really went somewhere.
  MOPPE_CHECK (length (a - Vec3 (45, 12, 62)) > 10.0f);
}

MOPPE_TEST (avatar_plants_its_stance_foot_while_walking) {
  // Between two poses a few steps apart in mid-stance, the planted foot
  // stays put in the world while the body moves on.
  const map::SurfaceGeometry surface =
    walker_test_ground ([] (float, float) { return 5.0f; });
  const game::WorldParams world = walker_test_world ();
  game::Walker walker;
  walker.spawn (position (Vec3 (20, 5, 64)), Vec3 (1, 0, 0));
  walker.set_walk (1.0f);
  walk_for (walker, 2.0f, surface, world);
  // Advance until the left foot is early in its stance.
  int guard = 0;
  while (std::fmod (walker.stride_phase (), 1.0f) > 0.05f && guard++ < 400)
    walker.update (seconds (walker_test_step), surface, world);
  const game::AvatarSkeleton before =
    game::pose_avatar (game::walker_pose (walker), 0.0f);
  walk_for (walker, 0.1f, surface, world);
  const game::AvatarSkeleton after =
    game::pose_avatar (game::walker_pose (walker), 0.0f);
  MOPPE_CHECK (after.pelvis[0] - before.pelvis[0] > 0.25f);
  MOPPE_CHECK_NEAR (after.ankle[0][0], before.ankle[0][0], 0.02f);
  MOPPE_CHECK_NEAR (after.ankle[0][1], before.ankle[0][1], 0.02f);
  // Limbs keep their lengths through the IK.
  MOPPE_CHECK_NEAR (
    length (after.knee[1] - after.hip[1]), game::avatar_size::thigh, 1e-3f);
  MOPPE_CHECK_NEAR (
    length (after.ankle[1] - after.knee[1]), game::avatar_size::shin, 1e-3f);
}
