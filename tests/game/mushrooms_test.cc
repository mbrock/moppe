#include <moppe/game/mushrooms.hh>
#include <moppe/game/scan.hh>

#include <tests/recording_renderer.hh>
#include <tests/test.hh>

using namespace moppe;
using namespace moppe::game;

namespace {
  MushroomSite mushroom_at (float x, float z, MushroomKind kind) {
    MushroomSite site;
    site.base = Vec3 (x, 10.0f, z);
    site.kind = kind;
    return site;
  }

  // A chanterelle ahead of a walker at (100, 100) facing +z, a porcini
  // behind, and a fly agaric far off.
  MushroomPatch mushroom_test_patch () {
    MushroomPlan plan;
    plan.sites.push_back (
      mushroom_at (100.0f, 101.5f, MushroomKind::chanterelle));
    plan.sites.push_back (mushroom_at (100.0f, 98.5f, MushroomKind::porcini));
    plan.sites.push_back (
      mushroom_at (140.0f, 100.0f, MushroomKind::fly_agaric));
    MushroomPatch patch;
    patch.rebuild (std::move (plan));
    return patch;
  }
}

MOPPE_TEST (a_walker_reaches_for_the_mushroom_in_front) {
  const MushroomPatch patch = mushroom_test_patch ();
  const Vec3 feet (100.0f, 10.0f, 100.0f);
  MOPPE_CHECK (patch.within_reach (feet, Vec3 (0, 0, 1)) == 0u);
  MOPPE_CHECK (patch.within_reach (feet, Vec3 (0, 0, -1)) == 1u);
  MOPPE_CHECK (
    !patch.within_reach (Vec3 (120.0f, 10.0f, 100.0f), Vec3 (0, 0, 1)));
  MOPPE_CHECK (patch.nearest (Vec3 (130.0f, 10.0f, 100.0f), 20.0f) == 2u);
}

MOPPE_TEST (picked_mushrooms_leave_the_ground_and_regrow_on_restore) {
  MushroomPatch patch = mushroom_test_patch ();
  const Vec3 feet (100.0f, 10.0f, 100.0f);
  Basket basket;
  basket.picked.push_back (0);
  patch.follow (basket);
  // With the one ahead gone, nothing is in front; the one behind is
  // reached by turning round.
  MOPPE_CHECK (!patch.within_reach (feet, Vec3 (0, 0, 1)));
  MOPPE_CHECK (patch.within_reach (feet, Vec3 (0, 0, -1)) == 1u);
  MOPPE_CHECK (patch.nearest (feet, 5.0f, std::array<std::uint32_t, 1> { 1 })
                 .has_value () == false);

  // An earlier, empty basket brings everything back.
  patch.follow (Basket {});
  MOPPE_CHECK (patch.within_reach (feet, Vec3 (0, 0, 1)) == 0u);
}

MOPPE_TEST (mushroom_tiles_mesh_near_the_camera_only) {
  MushroomPatch patch = mushroom_test_patch ();
  test::RecordingRenderer renderer;
  patch.draw (renderer, Vec3 (100.0f, 12.0f, 100.0f));
  MOPPE_CHECK (renderer.meshes_drawn >= 2);
  renderer.meshes_drawn = 0;
  patch.draw (renderer, Vec3 (900.0f, 12.0f, 900.0f));
  MOPPE_CHECK (renderer.meshes_drawn == 0);
}

MOPPE_TEST (fly_agaric_is_left_growing) {
  MOPPE_CHECK (!mushroom_edible (MushroomKind::fly_agaric));
  MOPPE_CHECK (mushroom_edible (MushroomKind::chanterelle));
  MOPPE_CHECK (mushroom_edible (MushroomKind::funnel_chanterelle));
  MOPPE_CHECK (mushroom_edible (MushroomKind::porcini));
}

MOPPE_TEST (the_scan_groups_mushrooms_and_lights_them_as_the_ring_passes) {
  // Three chanterelles together and a fly agaric among them, and one
  // chanterelle 40 m off: a group of three, the poisonous one alone, and
  // the far one alone.
  MushroomPlan plan;
  plan.sites = {
    mushroom_at (110.0f, 100.0f, MushroomKind::chanterelle),
    mushroom_at (111.0f, 100.5f, MushroomKind::chanterelle),
    mushroom_at (110.5f, 101.5f, MushroomKind::chanterelle),
    mushroom_at (110.5f, 100.8f, MushroomKind::fly_agaric),
    mushroom_at (100.0f, 140.0f, MushroomKind::chanterelle),
  };
  MushroomPatch patch;
  patch.rebuild (plan);
  const Vec3 origin (100.0f, 10.0f, 100.0f);
  const std::vector<ScanFind> finds = scan_mushrooms (patch, origin);
  MOPPE_CHECK (finds.size () == 3);
  int groups = 0, poisonous = 0, single = 0;
  const ScanFind* far = nullptr;
  for (const ScanFind& f : finds) {
    if (f.kind == ScanFind::Kind::poisonous)
      ++poisonous;
    else if (f.count == 3)
      ++groups;
    else if (f.count == 1) {
      ++single;
      far = &f;
    }
  }
  MOPPE_CHECK (groups == 1 && poisonous == 1 && single == 1);

  // The ring goes 24 m a second: at one second it has not reached the
  // chanterelle 40 m off, at two it has.
  Scan scan;
  scan.start (origin, 50.0, finds);
  MOPPE_CHECK (far != nullptr);
  const ScanFind& lone = scan.finds ()[far - finds.data ()];
  MOPPE_CHECK (scan.shown (lone, 51.0) == 0.0f);
  MOPPE_CHECK (scan.shown (lone, 52.0) > 0.5f);
  // And it fades once its time is up.
  MOPPE_CHECK (scan.shown (lone, 52.0 + scan_lit + scan_fade) == 0.0f);
  MOPPE_CHECK (!scan.showing (50.0 + 60.0));
}
