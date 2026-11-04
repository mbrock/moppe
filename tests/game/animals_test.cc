#include <moppe/game/animal_render.hh>
#include <moppe/game/animals.hh>

#include <tests/test.hh>

#include <cmath>

using namespace moppe;
using namespace moppe::game;

namespace {
  // A made country 2 km square: a gentle slope rising east, grazed open
  // within 400 m of a farm at (1000, 1000), forest beyond it, and a lake
  // of radius 60 m at (1150, 1000) beside the pasture.
  constexpr float animals_test_size = 2000.0f;

  bool animals_test_in_lake (float x, float z) {
    return std::hypot (x - 1150.0f, z - 1000.0f) < 60.0f;
  }

  AnimalGround animals_test_country () {
    return {
      .ground = [] (float x, float) { return 100.0f + 0.03f * x; },
      .dry = [] (float x, float z) { return !animals_test_in_lake (x, z); },
      .pasture =
        [] (float x, float z) {
          const bool open = std::hypot (x - 1000.0f, z - 1000.0f) < 400.0f;
          return Pasture { .grazed = open ? 0.85f : 0.0f,
                           .forest = open ? 0.0f : 0.5f };
        },
      .period_x = animals_test_size,
      .period_z = animals_test_size,
    };
  }

  Animals animals_test_farm () {
    const AnimalGround country = animals_test_country ();
    const Fold folds[] = { { .kind = Fold::Kind::farm,
                             .at = Vec3 (1000.0f, 130.0f, 1000.0f) } };
    Animals animals;
    animals.rebuild (plan_animals (country,
                                   folds,
                                   Vec3 (1800.0f, 0.0f, 1800.0f),
                                   animals_test_size,
                                   animals_test_size,
                                   7));
    return animals;
  }
}

MOPPE_TEST (the_stock_grazes_round_its_farm_and_keeps_out_of_the_water) {
  const AnimalGround country = animals_test_country ();
  Animals animals = animals_test_farm ();
  int stock = 0;
  for (const Animal& a : animals.animals ())
    stock += traits (a.species).stock;
  MOPPE_CHECK (stock >= 12);

  // Two minutes with nobody about: each of the stock spends time with
  // its head down in the grass, and some of them walk on to fresh grass,
  // all of them staying on the pasture and out of the lake.
  std::vector<float> cropping (animals.animals ().size (), 0.0f);
  std::vector<Vec3> start;
  for (const Animal& a : animals.animals ())
    start.push_back (a.position);
  const float dt = 1.0f / 30.0f;
  for (int step = 0; step < 120 * 30; ++step) {
    animals.step (country, {}, Vec3 (1000, 0, 1000), 5000.0f, dt);
    for (std::size_t i = 0; i < animals.animals ().size (); ++i) {
      const Animal& a = animals.animals ()[i];
      MOPPE_CHECK (!animals_test_in_lake (a.position[0], a.position[2]));
      if (a.head > 0.9f)
        cropping[i] += dt;
    }
  }
  int walked = 0;
  for (std::size_t i = 0; i < animals.animals ().size (); ++i) {
    const Animal& a = animals.animals ()[i];
    if (!traits (a.species).stock)
      continue;
    MOPPE_CHECK (cropping[i] > 20.0f);
    MOPPE_CHECK (std::hypot (a.position[0] - 1000.0f, a.position[2] - 1000.0f) <
                 420.0f);
    walked += std::hypot (a.position[0] - start[i][0],
                          a.position[2] - start[i][2]) > 5.0f;
  }
  MOPPE_CHECK (walked >= 3);
}

MOPPE_TEST (
  an_animal_moves_off_from_someone_coming_close_and_is_never_run_down) {
  const AnimalGround country = animals_test_country ();
  Animals animals = animals_test_farm ();
  // Pick a sheep, the warier of the stock, and walk straight at it.
  std::size_t sheep = 0;
  while (animals.animals ()[sheep].species != Species::sheep)
    ++sheep;
  const Vec3 at = animals.animals ()[sheep].position;
  Presence walker { .position = at + Vec3 (-15.0f, 0.0f, 0.0f),
                    .velocity = Vec3 (1.4f, 0.0f, 0.0f) };
  const float dt = 1.0f / 30.0f;
  bool fled = false;
  for (int step = 0; step < 8 * 30; ++step) {
    walker.position = walker.position + walker.velocity * dt;
    const Presence presences[] = { walker };
    animals.step (country, presences, at, 5000.0f, dt);
    fled |= animals.animals ()[sheep].doing == Animal::Doing::fleeing;
  }
  MOPPE_CHECK (fled);
  // The walker has passed well beyond where the sheep stood; the sheep
  // is away to one side or ahead, out of reach.
  const Vec3 now = animals.animals ()[sheep].position;
  MOPPE_CHECK (std::hypot (now[0] - walker.position[0],
                           now[2] - walker.position[2]) > 3.0f);

  // A machine driven straight through a herd at speed never overlaps an
  // animal: each is shoved aside and runs.
  Animals herd = animals_test_farm ();
  const Vec3 middle = herd.animals ()[0].position;
  Presence jeep { .position = middle + Vec3 (-40.0f, 0.0f, 0.0f),
                  .velocity = Vec3 (12.0f, 0.0f, 0.0f),
                  .machine = true };
  for (int step = 0; step < 7 * 30; ++step) {
    jeep.position = jeep.position + jeep.velocity * dt;
    const Presence presences[] = { jeep };
    herd.step (country, presences, middle, 5000.0f, dt);
    for (const Animal& a : herd.animals ())
      MOPPE_CHECK (std::hypot (a.position[0] - jeep.position[0],
                               a.position[2] - jeep.position[2]) >
                   0.4f * traits (a.species).length + 0.5f);
  }
}

MOPPE_TEST (a_grazing_animal_reaches_the_grass_with_its_muzzle) {
  const auto ground = [] (float x, float) { return 10.0f + 0.1f * x; };
  for (int s = 0; s < species_count; ++s) {
    Animal a;
    a.species = static_cast<Species> (s);
    a.position = Vec3 (3.0f, ground (3.0f, 0.0f), 0.0f);
    a.heading = 0.4f;
    a.head = 1.0f;
    // The moose browses the shrubs rather than the grass.
    const Vec3 muzzle = animal_muzzle (a, ground);
    const float above = a.species == Species::moose ? 0.55f : 0.0f;
    MOPPE_CHECK_NEAR (muzzle[1], ground (muzzle[0], muzzle[2]) + above, 0.12f);
    // Head up, it is well clear of the ground.
    a.head = 0.0f;
    const Vec3 up = animal_muzzle (a, ground);
    MOPPE_CHECK (up[1] - ground (up[0], up[2]) > 0.35f);
  }
}

// In a barn the stock is kept: a cow tied in its stall stands there with
// its head to the manger however long it is watched, and the pigs root
// about their pen without leaving it.
MOPPE_TEST (the_barns_stock_stays_in_its_stalls_and_pen) {
  const AnimalGround country = animals_test_country ();
  const Vec3 stall (1000.0f, 130.0f, 1500.0f), pen (1004.0f, 130.0f, 1500.0f);
  const Stall stalls[] = {
    { .species = Species::cow, .at = stall, .heading = 1.2f },
    { .species = Species::pig,
      .at = pen,
      .heading = 0.3f,
      .half_along = 1.4f,
      .half_across = 1.0f,
      .count = 3 },
  };
  Animals animals;
  animals.rebuild (plan_animals (country,
                                 {},
                                 Vec3 (1800.0f, 0.0f, 1800.0f),
                                 animals_test_size,
                                 animals_test_size,
                                 7,
                                 stalls));
  int cows = 0, pigs = 0;
  for (const Animal& a : animals.animals ())
    if (a.indoors)
      (a.species == Species::cow ? cows : pigs) += 1;
  MOPPE_CHECK (cows == 1 && pigs == 3);
  const float dt = 1.0f / 30.0f;
  bool moved = false;
  std::vector<Vec3> start;
  for (const Animal& a : animals.animals ())
    start.push_back (a.position);
  for (int step = 0; step < 180 * 30; ++step) {
    // Someone walks back and forth before them.
    const float x = 1002.0f + 4.0f * std::sin (step * dt * 0.3f);
    const Presence walker { .position = Vec3 (x, 130.0f, 1497.0f) };
    const Presence presences[] = { walker };
    animals.step (country, presences, stall, 500.0f, dt);
    for (std::size_t i = 0; i < animals.animals ().size (); ++i) {
      const Animal& a = animals.animals ()[i];
      if (!a.indoors)
        continue;
      if (a.species == Species::cow) {
        MOPPE_CHECK (std::hypot (a.position[0] - stall[0],
                                 a.position[2] - stall[2]) < 1e-3f);
        MOPPE_CHECK_NEAR (a.heading, 1.2f, 1e-4f);
        continue;
      }
      // Within the pen, in its own frame.
      const Vec3 along (std::sin (0.3f), 0.0f, std::cos (0.3f));
      const Vec3 across (along[2], 0.0f, -along[0]);
      const Vec3 d = a.position - pen;
      MOPPE_CHECK (std::abs (dot (d, along)) <= 1.4f + 1e-3f);
      MOPPE_CHECK (std::abs (dot (d, across)) <= 1.0f + 1e-3f);
      if (length (a.position - start[i]) > 0.3f)
        moved = true;
    }
  }
  MOPPE_CHECK (moved);
}
