#include <moppe/game/hud.hh>
#include <tests/test.hh>

#include <algorithm>
#include <vector>

using namespace moppe;

namespace {
  using Mark = game::HudState::Waymark;

  Mark at (unsigned who, float x, float y, bool in_view = true) {
    Mark mark;
    mark.who = who;
    mark.x = x;
    mark.y = y;
    mark.in_view = in_view;
    mark.alpha = 1.0f;
    return mark;
  }

  // A marker stands over its members' middle, 40 points square alone
  // and 30 wider for each further member, as a group's block widens
  // with what it lists.
  std::vector<unsigned> grouped (const std::vector<Mark>& marks,
                                 std::vector<unsigned> before = {}) {
    auto groups = game::group_waymarks (
      marks,
      [&] (unsigned group) {
        float x = 0.0f, y = 0.0f, n = 0.0f;
        for (unsigned i = 0; i < marks.size (); ++i)
          if (group & (1u << i)) {
            x += marks[i].x;
            y += marks[i].y;
            n += 1.0f;
          }
        x /= n;
        y /= n;
        const float half = 20.0f + 15.0f * (n - 1.0f);
        return game::HudBox { x - half, y - 20.0f, x + half, y + 20.0f };
      },
      before);
    std::ranges::sort (groups);
    return groups;
  }
}

MOPPE_TEST (waymarks_whose_markers_would_meet_share_one) {
  // Boxes 40 wide: centres 45 apart leave a 5-point gap, which joins;
  // 47 apart leave 7, which does not.
  MOPPE_CHECK (
    grouped ({ at (Mark::Bike, 100, 100), at (Mark::Jeep, 145, 100) }) ==
    std::vector<unsigned> { 3 });
  MOPPE_CHECK (
    grouped ({ at (Mark::Bike, 100, 100), at (Mark::Jeep, 147, 100) }) ==
    (std::vector<unsigned> { 1, 2 }));
  // The other player and a place join machines just as machines do.
  MOPPE_CHECK (grouped ({ at (Mark::Companion, 100, 100),
                          at (Mark::Place, 100, 120),
                          at (Mark::Boat, 500, 100) }) ==
               (std::vector<unsigned> { 3, 4 }));
  // A mark in view and one pinned to the edge point different ways, and
  // a faded-out mark is not drawn at all.
  MOPPE_CHECK (
    grouped ({ at (Mark::Bike, 100, 100), at (Mark::Jeep, 100, 100, false) }) ==
    (std::vector<unsigned> { 1, 2 }));
  auto faded = at (Mark::Jeep, 100, 100);
  faded.alpha = 0.0f;
  MOPPE_CHECK (grouped ({ at (Mark::Bike, 100, 100), faded }) ==
               std::vector<unsigned> { 1 });
}

MOPPE_TEST (waymark_groups_grow_until_no_two_markers_meet) {
  // The third's marker keeps 20 points clear of either of the first
  // two's alone, but the wider marker of the pair they make reaches it.
  MOPPE_CHECK (grouped ({ at (Mark::Bike, 0, 0),
                          at (Mark::Jeep, 0, 30),
                          at (Mark::Tractor, 60, 15),
                          at (Mark::Caddy, 400, 0) }) ==
               (std::vector<unsigned> { 7, 8 }));
}

MOPPE_TEST (waymark_groups_part_only_past_a_wider_gap) {
  // 20 points apart: alone they stay apart, but a group they formed
  // last frame holds; at 31 it lets go.
  const std::vector<Mark> close { at (Mark::Bike, 0, 0),
                                  at (Mark::Jeep, 60, 0) };
  MOPPE_CHECK (grouped (close) == (std::vector<unsigned> { 1, 2 }));
  MOPPE_CHECK (grouped (close, { Mark::Bike | Mark::Jeep | Mark::Boat }) ==
               std::vector<unsigned> { 3 });
  MOPPE_CHECK (grouped ({ at (Mark::Bike, 0, 0), at (Mark::Jeep, 71, 0) },
                        { Mark::Bike | Mark::Jeep }) ==
               (std::vector<unsigned> { 1, 2 }));
  // Being together with someone else does not count.
  MOPPE_CHECK (grouped (close, { Mark::Bike | Mark::Boat }) ==
               (std::vector<unsigned> { 1, 2 }));
}
