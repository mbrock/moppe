#include <moppe/game/hud.hh>
#include <tests/test.hh>

#include <cmath>
#include <string_view>

using namespace moppe;

MOPPE_TEST (
  vehicle_finders_bundle_all_vehicles_without_swallowing_the_companion) {
  game::HudState state;
  state.waymark_count = 8;
  auto& companion = state.waymarks[0];
  companion.name = "companion";
  companion.x = 640;
  companion.y = 200;
  companion.metres = 445;
  companion.alpha = 1;
  companion.in_view = true;
  for (int i = 1; i < 8; ++i) {
    auto& mark = state.waymarks[i];
    mark.vehicles = 1u << (i - 1);
    mark.position = Vec3 (i * 7, 0, i * 3);
    mark.x = 620 + i * 4;
    mark.y = 200;
    mark.metres = 470 - i * 5;
    mark.alpha = 0.5f + i * 0.05f;
    mark.in_view = true;
  }
  game::bundle_waymarks (state);
  MOPPE_CHECK (state.waymark_count == 2);
  MOPPE_CHECK (state.waymarks[0].vehicles == 0);
  MOPPE_CHECK (std::string_view (state.waymarks[0].name) == "companion");
  const auto& bundle = state.waymarks[1];
  MOPPE_CHECK (bundle.vehicles == 127);
  MOPPE_CHECK_NEAR (bundle.metres, 435, 0);
  MOPPE_CHECK_NEAR (bundle.x, 636, 0);
  MOPPE_CHECK_NEAR (bundle.alpha, 0.85f, 1e-6f);
}

MOPPE_TEST (vehicle_finders_need_both_world_and_screen_proximity) {
  struct Case {
    float separation, dx, dy;
    bool in_view;
    int bundles;
  };
  // Both sides of the world-space and projected-space boundaries. A
  // distant machine on the same bearing must keep its own distance.
  for (const auto& c : { Case { 80, 96, 48, false, 1 },
                         Case { 80.1f, 0, 0, false, 2 },
                         Case { 10, 96.1f, 0, false, 2 },
                         Case { 10, 0, 48.1f, false, 2 },
                         Case { 10, 0, 0, true, 2 } }) {
    game::HudState state;
    state.waymark_count = 2;
    auto& a = state.waymarks[0];
    a.vehicles = game::HudState::Waymark::Bike;
    a.alpha = 1;
    auto& b = state.waymarks[1];
    b.vehicles = game::HudState::Waymark::Jeep;
    b.position = Vec3 (0, 0, c.separation);
    b.x = c.dx;
    b.y = c.dy;
    b.in_view = c.in_view;
    b.alpha = 1;
    game::bundle_waymarks (state);
    MOPPE_CHECK (state.waymark_count == c.bundles);
  }
}

MOPPE_TEST (vehicle_finder_bundles_keep_bridged_members_and_edge_direction) {
  game::HudState state;
  state.waymark_count = 3;
  for (int i = 0; i < 3; ++i) {
    auto& mark = state.waymarks[i];
    mark.vehicles = 1u << i;
    mark.x = i == 1 ? 150 : i == 2 ? 75 : 0;
    mark.metres = 100 + i * 10;
    mark.alpha = 1;
    // Off-screen to the left, straddling atan2's -pi/pi seam.
    mark.angle = i == 1 ? -3.1f : 3.1f;
  }
  game::bundle_waymarks (state);
  MOPPE_CHECK (state.waymark_count == 1);
  MOPPE_CHECK (state.waymarks[0].vehicles == 7);
  MOPPE_CHECK_NEAR (state.waymarks[0].x, 75, 0);
  MOPPE_CHECK (std::cos (state.waymarks[0].angle) < -0.99f);

  // The same crowding must not chain machines across a wide area.
  state.waymark_count = 3;
  for (int i = 0; i < 3; ++i) {
    state.waymarks[i].vehicles = 1u << i;
    state.waymarks[i].position = Vec3 (i * 60, 0, 0);
    state.waymarks[i].x = 75;
    state.waymarks[i].alpha = 1;
  }
  game::bundle_waymarks (state);
  MOPPE_CHECK (state.waymark_count == 2);
  MOPPE_CHECK (state.waymarks[0].vehicles == 3);
  MOPPE_CHECK (state.waymarks[1].vehicles == 4);
}
