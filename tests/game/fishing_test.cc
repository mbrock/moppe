#include <moppe/game/fishing.hh>

#include <tests/test.hh>

using namespace moppe;
using namespace moppe::game;

MOPPE_TEST (the_water_decides_what_bites) {
  bool running_trout = false, sea_cod = false;
  for (std::uint32_t seed = 0; seed < 400; ++seed) {
    const Fish river = fish_for (Waters::running, false, seed);
    MOPPE_CHECK (river.kind == FishKind::brown_trout ||
                 river.kind == FishKind::grayling ||
                 river.kind == FishKind::perch);
    running_trout |= river.kind == FishKind::brown_trout;
    const Fish lake = fish_for (Waters::still, false, seed);
    MOPPE_CHECK (lake.kind != FishKind::cod && lake.kind != FishKind::grayling);
    MOPPE_CHECK (lake.length > 0.1f && lake.length < 1.0f);
    sea_cod |= fish_for (Waters::sea, false, seed).kind == FishKind::cod;
    const float wait = bite_wait (seed);
    MOPPE_CHECK (wait >= 4.0f && wait <= 16.0f);
  }
  MOPPE_CHECK (running_trout && sea_cod);
  MOPPE_CHECK (fish_for (Waters::still, true, 9).kind != FishKind::roach &&
               fish_for (Waters::still, true, 9).kind != FishKind::pike);
}

MOPPE_TEST (a_struck_fish_is_landed_into_the_creel) {
  Fishing fishing;
  cast_line (
    fishing.line, Vec3 (0, 1, 0), Vec3 (0, 0, 8), Waters::still, false, 10.0);
  MOPPE_CHECK (line_out (fishing.line));
  MOPPE_CHECK (rod_pose (fishing.line, 10.2, 0.0f).has_value ());
  // The float lands, and lies there until the take.
  tend_line (fishing, 10.0 + cast_seconds + 0.01, false);
  MOPPE_CHECK (fishing.line.phase == Line::Phase::waiting);
  const double take = fishing.line.bite_at;
  MOPPE_CHECK (take > 10.0 + cast_seconds + 3.9);
  tend_line (fishing, take + 0.01, false);
  MOPPE_CHECK (fishing.line.phase == Line::Phase::biting);
  // Struck, it is fought in, and then it is in the creel.
  MOPPE_CHECK (tend_line (fishing, take + 0.3, true));
  MOPPE_CHECK (fishing.line.phase == Line::Phase::playing);
  const double fought = take + 0.3 + play_seconds (fishing.line.fish) + 0.1;
  tend_line (fishing, fought, false);
  MOPPE_CHECK (fishing.line.phase == Line::Phase::landed);
  MOPPE_CHECK (fishing.creel.kept () == 1 && !fishing.creel.last_lost);
  tend_line (fishing, fought + landed_seconds + 0.1, false);
  MOPPE_CHECK (!line_out (fishing.line));
  MOPPE_CHECK (!rod_pose (fishing.line, fought + 5.0, 0.0f).has_value ());
}

MOPPE_TEST (a_fish_not_struck_gets_away) {
  Fishing fishing;
  cast_line (
    fishing.line, Vec3 (0, 1, 0), Vec3 (0, 0, 8), Waters::running, false, 0.0);
  tend_line (fishing, cast_seconds + 0.01, false);
  const double take = fishing.line.bite_at;
  tend_line (fishing, take + 0.01, false);
  tend_line (fishing, take + strike_seconds + 0.1, false);
  MOPPE_CHECK (fishing.line.phase == Line::Phase::waiting);
  MOPPE_CHECK (fishing.creel.last_lost && fishing.creel.kept () == 0);
  MOPPE_CHECK (fishing.line.bite_at > take + strike_seconds);
  // Winding in an empty line puts the rod away.
  MOPPE_CHECK (tend_line (fishing, take + 2.0, true));
  MOPPE_CHECK (fishing.line.phase == Line::Phase::empty);
  tend_line (fishing, take + 2.0 + wind_seconds + 0.1, false);
  MOPPE_CHECK (!line_out (fishing.line));
}
