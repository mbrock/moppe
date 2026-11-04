#include <moppe/game/hens.hh>

#include <tests/test.hh>

#include <cmath>

using namespace moppe;
using namespace moppe::game;

namespace {
  // A barn standing on level ground at 10 m, its front facing +z, its
  // floor 0.34 m up inside its walls and on the ramp before its doors.
  AnimalGround hens_test_barnyard () {
    return {
      .ground =
        [] (float x, float z) {
          if (std::abs (x) < 5.86f && std::abs (z) < 4.0f)
            return 10.34f;
          return 10.0f;
        },
      .dry = [] (float, float) { return true; },
      .pasture = [] (float, float) { return Pasture { .grazed = 0.8f }; },
    };
  }
}

// At dusk the hens and the rooster go in and up onto the perches, and at
// dawn they come down again and go about the barn and the yard.
MOPPE_TEST (hens_roost_on_the_perches_at_night_and_come_down_by_day) {
  const AnimalGround barnyard = hens_test_barnyard ();
  const Coop coops[] = { { .frame = Mat4::translation (Vec3 (0, 10, 0)) } };
  Hens hens;
  hens.rebuild ({ coops[0] }, plan_birds (coops, 3));
  int adults = 0, chicks = 0;
  for (const Bird& b : hens.birds ())
    (b.kind == Bird::Kind::chick ? chicks : adults) += 1;
  MOPPE_CHECK (adults >= 5 && chicks >= 3);
  const float dt = 1.0f / 30.0f;
  for (int step = 0; step < 150 * 30; ++step)
    hens.step (barnyard, {}, Vec3 (), 500.0f, 21.0f, dt);
  for (const Bird& b : hens.birds ()) {
    if (b.kind == Bird::Kind::chick)
      continue;
    // All but the mother, who sits on the floor below with her brood.
    const bool mother = std::ranges::any_of (hens.birds (), [&] (auto& c) {
      return c.mother == &b - hens.birds ().data ();
    });
    if (mother) {
      MOPPE_CHECK (b.position[1] < 10.5f && std::abs (b.position[0]) < 5.86f &&
                   std::abs (b.position[2]) < 4.0f);
      continue;
    }
    MOPPE_CHECK (b.doing == Bird::Doing::roosting);
    MOPPE_CHECK (b.position[1] > 10.9f);
  }
  // Morning.
  for (int step = 0; step < 60 * 30; ++step)
    hens.step (barnyard, {}, Vec3 (), 500.0f, 8.0f, dt);
  for (const Bird& b : hens.birds ()) {
    MOPPE_CHECK (b.doing != Bird::Doing::roosting);
    MOPPE_CHECK (b.position[1] < 10.5f);
  }
}

// The boxes start with an egg or two, are laid in every few hours, hold
// no more than three, and are reached into only by someone standing
// before them.
MOPPE_TEST (eggs_are_laid_in_the_nest_boxes_and_taken_from_before_them) {
  std::vector<NestState> nests = first_eggs (4, 9.0, 11);
  for (const NestState& n : nests) {
    MOPPE_CHECK (n.eggs >= 1 && n.eggs <= 2);
    MOPPE_CHECK (n.next_lay > 9.0);
  }
  // Emptied, within half a day each is laid in again...
  for (NestState& n : nests)
    n.eggs = 0;
  lay_eggs (nests, 21.0, 11);
  for (const NestState& n : nests)
    MOPPE_CHECK (n.eggs >= 1 && n.eggs <= nest_holds);
  // ...but left for days, holds no more than it can.
  lay_eggs (nests, 21.0 + 24.0 * 5, 11);
  for (const NestState& n : nests) {
    MOPPE_CHECK (n.eggs == nest_holds);
    MOPPE_CHECK (n.next_lay > 21.0 + 24.0 * 5);
  }

  const Nest boxes[] = {
    { .at = Vec3 (0.0f, 10.9f, 0.0f), .out = Vec3 (1, 0, 0) },
    { .at = Vec3 (0.0f, 10.9f, 0.55f), .out = Vec3 (1, 0, 0) },
  };
  std::vector<NestState> states = { { .eggs = 1 }, { .eggs = 2 } };
  const Vec3 feet (0.7f, 10.34f, 0.45f);
  // Facing them, the nearer box with an egg in it.
  MOPPE_CHECK (nest_within_reach (boxes, states, feet, Vec3 (-1, 0, 0)) ==
               std::optional<std::size_t> (1));
  // Its eggs gone, the other one.
  states[1].eggs = 0;
  MOPPE_CHECK (nest_within_reach (boxes, states, feet, Vec3 (-1, 0, 0)) ==
               std::optional<std::size_t> (0));
  // Turned away from them, or too far off, none.
  MOPPE_CHECK (!nest_within_reach (boxes, states, feet, Vec3 (1, 0, 0)));
  MOPPE_CHECK (!nest_within_reach (
    boxes, states, feet + Vec3 (2.0f, 0, 0), Vec3 (-1, 0, 0)));
}
