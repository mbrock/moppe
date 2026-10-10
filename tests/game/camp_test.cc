#include <moppe/game/camp.hh>

#include <tests/test.hh>

#include <cmath>
#include <vector>

using namespace moppe;
using namespace moppe::game;

namespace {
  // A birch: tall, a hand's breadth through, bare to well above the head.
  mov::Trunk camp_test_birch (float x, float z, float ground = 10.0f) {
    return { .root = Vec3 (x, ground, z),
             .axis = Vec3 (0, 1, 0),
             .height = 12.0f,
             .radius = 0.12f,
             .clear = 6.0f };
  }

  // A spruce of the same size keeps its boughs to the ground.
  mov::Trunk camp_test_spruce (float x, float z) {
    mov::Trunk spruce = camp_test_birch (x, z);
    spruce.clear = 0.8f;
    return spruce;
  }

  float camp_test_level (float, float) {
    return 10.0f;
  }
}

MOPPE_TEST (a_hammock_hangs_between_two_fit_trees) {
  const std::vector<mov::Trunk> trees = { camp_test_birch (100.0f, 100.0f),
                                          camp_test_birch (104.0f, 100.0f) };
  const Vec3 between (102.0f, 10.0f, 100.3f);
  const std::optional<HammockSite> site =
    hammock_site_at (trees, between, camp_test_level);
  MOPPE_CHECK (site.has_value ());
  if (!site)
    return;
  // The straps are level, within reach, and on the trunks.
  MOPPE_CHECK_NEAR (site->strap[0][1], site->strap[1][1], 1e-5f);
  MOPPE_CHECK (site->strap[0][1] > 10.9f && site->strap[0][1] < 12.5f);
  MOPPE_CHECK_NEAR (
    std::fabs (site->strap[1][0] - site->strap[0][0]), 4.0f, 1e-4f);
  MOPPE_CHECK_NEAR (hammock_distance (*site, between), 0.3f, 1e-4f);
  MOPPE_CHECK_NEAR (length (hammock_along (*site)), 1.0f, 1e-5f);

  // Not from outside the pair, nor from far off to one side.
  MOPPE_CHECK (
    !hammock_site_at (trees, Vec3 (97.0f, 10.0f, 100.0f), camp_test_level));
  MOPPE_CHECK (
    !hammock_site_at (trees, Vec3 (102.0f, 10.0f, 103.0f), camp_test_level));
}

MOPPE_TEST (unfit_trees_carry_no_hammock) {
  const Vec3 between (102.0f, 10.0f, 100.0f);
  const auto hangs = [&] (std::vector<mov::Trunk> trees) {
    return hammock_site_at (trees, between, camp_test_level).has_value ();
  };
  // Too near, too far.
  MOPPE_CHECK (!hangs (
    { camp_test_birch (101.0f, 100.0f), camp_test_birch (103.2f, 100.0f) }));
  MOPPE_CHECK (!hangs (
    { camp_test_birch (99.0f, 100.0f), camp_test_birch (105.5f, 100.0f) }));
  // A spruce's skirts are in the way.
  MOPPE_CHECK (!hangs (
    { camp_test_spruce (100.0f, 100.0f), camp_test_birch (104.0f, 100.0f) }));
  // A sapling will not hold.
  mov::Trunk sapling = camp_test_birch (104.0f, 100.0f);
  sapling.height = 3.0f;
  sapling.radius = 0.03f;
  MOPPE_CHECK (!hangs ({ camp_test_birch (100.0f, 100.0f), sapling }));
  // A third tree stands between them.
  MOPPE_CHECK (!hangs ({ camp_test_birch (100.0f, 100.0f),
                         camp_test_birch (104.0f, 100.0f),
                         camp_test_birch (102.0f, 100.2f) }));
  // One stands a long step above the other.
  MOPPE_CHECK (!hangs ({ camp_test_birch (100.0f, 100.0f),
                         camp_test_birch (104.0f, 100.0f, 11.2f) }));
  // The ground rises into where the cloth would hang.
  const auto hummock = [] (float x, float) {
    return 10.0f + (std::fabs (x - 102.0f) < 0.6f ? 0.8f : 0.0f);
  };
  MOPPE_CHECK (!hammock_site_at (
    std::vector<mov::Trunk> { camp_test_birch (100.0f, 100.0f),
                              camp_test_birch (104.0f, 100.0f) },
    between,
    hummock));
}

MOPPE_TEST (the_head_lies_toward_the_higher_root) {
  const std::vector<mov::Trunk> trees = {
    camp_test_birch (100.0f, 100.0f, 10.5f), camp_test_birch (104.0f, 100.0f)
  };
  const std::optional<HammockSite> site =
    hammock_site_at (trees, Vec3 (102.0f, 10.0f, 100.0f), camp_test_level);
  MOPPE_CHECK (site.has_value ());
  if (!site)
    return;
  MOPPE_CHECK_NEAR (site->strap[1][0], 100.0f, 1e-4f);
  MOPPE_CHECK_NEAR (hammock_along (*site)[0], -1.0f, 1e-5f);
}

MOPPE_TEST (the_nearest_site_prefers_open_sky_when_asked) {
  // A pair close by among spruce, and one farther off in the open.
  std::vector<mov::Trunk> trees = { camp_test_birch (100.0f, 100.0f),
                                    camp_test_birch (104.0f, 100.0f),
                                    camp_test_birch (160.0f, 100.0f),
                                    camp_test_birch (164.0f, 100.0f) };
  for (int i = 0; i < 6; ++i)
    trees.push_back (camp_test_spruce (98.0f + 2.0f * i, 104.0f));
  const Vec3 near (100.0f, 10.0f, 96.0f);
  const std::optional<HammockSite> nearest =
    nearest_hammock_site (trees, near, camp_test_level);
  const std::optional<HammockSite> open =
    nearest_hammock_site (trees, near, camp_test_level, 60.0f);
  MOPPE_CHECK (nearest.has_value () && open.has_value ());
  if (!nearest || !open)
    return;
  MOPPE_CHECK_NEAR (hammock_middle (*nearest)[0], 102.0f, 1e-3f);
  MOPPE_CHECK_NEAR (hammock_middle (*open)[0], 162.0f, 1e-3f);
}

MOPPE_TEST (a_sleeper_lies_in_the_sag_of_the_cloth) {
  const std::vector<mov::Trunk> trees = { camp_test_birch (100.0f, 100.0f),
                                          camp_test_birch (104.0f, 100.0f) };
  const HammockSite site =
    *hammock_site_at (trees, Vec3 (102.0f, 10.0f, 100.0f), camp_test_level);
  const AvatarSkeleton lying = pose_resting (site, 0.0f, Vec3 (0, 1, 0), 0.0f);
  // Between the trees, below the straps, clear of the ground, on their
  // back with their head toward the second tree.
  MOPPE_CHECK (lying.pelvis[0] > 101.0f && lying.pelvis[0] < 103.0f);
  MOPPE_CHECK (lying.pelvis[1] < site.strap[0][1] - 0.3f);
  MOPPE_CHECK (lying.pelvis[1] > 10.45f);
  MOPPE_CHECK (lying.forward[1] > 0.9f);
  MOPPE_CHECK (lying.head[0] > lying.pelvis[0] + 0.4f);
  MOPPE_CHECK (lying.ankle[0][0] < lying.pelvis[0] - 0.5f);
  // Their eyes look up and along their own length.
  const Vec3 eye = resting_eye (site, 0.0f);
  MOPPE_CHECK (length (eye - lying.head) < 0.3f);
  const Vec3 gaze = resting_gaze (site, 0.0f, 1.1f);
  MOPPE_CHECK (gaze[1] > 0.8f && gaze[0] < 0.0f);
  // A swing carries the sleeper sideways.
  const AvatarSkeleton swung = pose_resting (site, 0.3f, Vec3 (0, 1, 0), 0.0f);
  MOPPE_CHECK (std::fabs (swung.pelvis[2] - lying.pelvis[2]) > 0.1f);
}

MOPPE_TEST (the_fire_catches_burns_and_dies) {
  Camp camp;
  MOPPE_CHECK (fire_burn (camp, 10.0) == 0.0f);
  camp.fire = true;
  camp.fire_time = 10.0;
  camp.hearth = Vec3 (5.0f, 2.0f, 7.0f);
  MOPPE_CHECK (fire_burn (camp, 10.0) > 0.0f);
  MOPPE_CHECK (fire_burn (camp, 11.0) < fire_burn (camp, 13.0));
  MOPPE_CHECK_NEAR (fire_burn (camp, 20.0), 1.0f, 1e-5f);
  MOPPE_CHECK (fire_light_position (camp)[1] > 2.2f);
  // Its light is warm and never still.
  const DisplayColor light = fire_light_color (1.0f, 3.0f);
  MOPPE_CHECK (light.red > light.green && light.green > light.blue);
  MOPPE_CHECK (fire_light_color (1.0f, 3.1f).red != light.red);
  camp.fire = false;
  camp.fire_time = 30.0;
  MOPPE_CHECK (fire_burn (camp, 30.5) < 1.0f);
  MOPPE_CHECK (fire_burn (camp, 32.0) == 0.0f);

  // Getting in and out of the hammock eases its load.
  camp.resting = true;
  camp.rest_time = 40.0;
  MOPPE_CHECK (hammock_load (camp, 40.0) == 0.0f);
  MOPPE_CHECK_NEAR (hammock_load (camp, 42.0), 1.0f, 1e-5f);
  camp.resting = false;
  camp.rest_time = 50.0;
  MOPPE_CHECK_NEAR (hammock_load (camp, 50.0), 1.0f, 1e-5f);
  MOPPE_CHECK (hammock_load (camp, 52.0) == 0.0f);
}
