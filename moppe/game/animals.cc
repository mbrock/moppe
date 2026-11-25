#include <moppe/game/animals.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>

namespace moppe::game {
  namespace {
    constexpr float animals_tau = 6.28318531f;

    constexpr std::array<SpeciesTraits, species_count> species_traits = { {
      { .name = "horse",
        .plural = "horses",
        .walk = 1.5f * u::m / u::s,
        .run = 4.5f * u::m / u::s,
        .wary = 5.0f * u::m,
        .flight = 25.0f * u::m,
        .length = 1.7f * u::m,
        .stride = 1.7f * u::m,
        .stock = true },
      { .name = "cow",
        .plural = "cows",
        .walk = 1.1f * u::m / u::s,
        .run = 3.0f * u::m / u::s,
        .wary = 5.0f * u::m,
        .flight = 18.0f * u::m,
        .length = 1.6f * u::m,
        .stride = 1.4f * u::m,
        .stock = true },
      { .name = "sheep",
        .plural = "sheep",
        .walk = 0.9f * u::m / u::s,
        .run = 3.2f * u::m / u::s,
        .wary = 9.0f * u::m,
        .flight = 22.0f * u::m,
        .length = 0.95f * u::m,
        .stride = 0.75f * u::m,
        .stock = true },
      { .name = "reindeer",
        .plural = "reindeer",
        .walk = 1.4f * u::m / u::s,
        .run = 6.0f * u::m / u::s,
        .wary = 30.0f * u::m,
        .flight = 70.0f * u::m,
        .length = 1.3f * u::m,
        .stride = 1.4f * u::m },
      { .name = "moose",
        .plural = "moose",
        .walk = 1.5f * u::m / u::s,
        .run = 6.5f * u::m / u::s,
        .wary = 28.0f * u::m,
        .flight = 80.0f * u::m,
        .length = 1.9f * u::m,
        .stride = 2.0f * u::m },
      { .name = "roe deer",
        .plural = "roe deer",
        .walk = 1.0f * u::m / u::s,
        .run = 8.0f * u::m / u::s,
        .wary = 38.0f * u::m,
        .flight = 90.0f * u::m,
        .length = 0.9f * u::m,
        .stride = 0.9f * u::m,
        .bounds = true },
      { .name = "pig",
        .plural = "pigs",
        .walk = 0.7f * u::m / u::s,
        .run = 2.4f * u::m / u::s,
        .wary = 3.0f * u::m,
        .flight = 6.0f * u::m,
        .length = 1.1f * u::m,
        .stride = 0.6f * u::m,
        .stock = true },
    } };

    std::uint32_t animals_hash (std::uint32_t a, std::uint32_t b) {
      std::uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u);
      h ^= h >> 16;
      h *= 0x85ebca6bu;
      h ^= h >> 13;
      h *= 0xc2b2ae35u;
      h ^= h >> 16;
      return h;
    }

    float animals_unit (std::uint32_t a, std::uint32_t b) {
      return static_cast<float> (animals_hash (a, b) >> 8) *
             (1.0f / 16777216.0f);
    }

    float smooth01 (float a, float b, float x) {
      const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    float smooth01 (meters_t a, meters_t b, meters_t x) {
      return smooth01 (meters_value (a), meters_value (b), meters_value (x));
    }

    // A place moved by a displacement.
    position_t offset (const position_t& at, const displacement_t& by) {
      return at + quantity_cast<isq::position_vector> (by);
    }

    // The shortest way from `from` to `to` in a periodic world.
    plan_displacement_t wrapped (const plan_position_t& from,
                                 const plan_position_t& to,
                                 const AnimalGround& g) {
      return terrain::torus_offset (from, to, g.period);
    }

    // How far, as std::hypot measures it.
    meters_t flat_length (const plan_displacement_t& d) {
      return std::hypot (plan_value (d).x (), plan_value (d).z ()) * u::m;
    }

    meters_t flat_distance (const plan_position_t& a,
                            const plan_position_t& b,
                            const AnimalGround& g) {
      return flat_length (wrapped (a, b, g));
    }

    heading_t heading_of (const plan_displacement_t& d) {
      return std::atan2 (plan_value (d).x (), plan_value (d).z ()) *
             heading[u::rad];
    }

    direction_t forward_of (heading_t heading) {
      const float h = heading.numerical_value_in (u::rad);
      return unit_direction (Vec3 (std::sin (h), 0.0f, std::cos (h)));
    }

    plan_direction_t plan_forward_of (heading_t heading) {
      const float h = heading.numerical_value_in (u::rad);
      return plan_unit_direction (Vec2 (std::sin (h), std::cos (h)));
    }

    radians_t angle_between (heading_t a, heading_t b) {
      return std::remainder ((b - a).numerical_value_in (u::rad), animals_tau) *
             u::rad;
    }

    // How steep the ground is about a point, rise over run.
    quantity<terrain::terrain_slope[one], float>
    steepness (const AnimalGround& g, const plan_position_t& at) {
      const meters_t e = 2.0f * u::m;
      const Vec2& p = plan_value (at);
      const float x = p.x (), z = p.z (), run = meters_value (e);
      const terrain::ElevationChange dx =
        g.ground (plan_position (x + run, z)) -
        g.ground (plan_position (x - run, z));
      const terrain::ElevationChange dz =
        g.ground (plan_position (x, z + run)) -
        g.ground (plan_position (x, z - run));
      return std::hypot (dx.numerical_value_in (u::m),
                         dz.numerical_value_in (u::m)) /
             (2.0f * run) * terrain::terrain_slope[one];
    }

    // Whether an animal can stand at a point.
    bool footing (const AnimalGround& g, const plan_position_t& at) {
      return g.dry (at) &&
             steepness (g, at) < 0.55f * terrain::terrain_slope[one];
    }

    // How much a species wants to be at a point, 0..1. Built roads and their
    // immediate verges offer nothing to graze or browse.
    proportion_t liking (Species species, const Pasture& p) {
      constexpr proportion_t whole = 1.0f * proportion[one];
      const proportion_t off_road = whole - p.road;
      proportion_t base = 0.0f * proportion[one];
      switch (species) {
      case Species::horse:
      case Species::cow:
      case Species::sheep:
      case Species::pig:
        base = quantity_cast<proportion> (p.grazed * (whole - 0.8f * p.forest) *
                                          (whole - 0.7f * p.bog));
        break;
      case Species::moose:
        // Browse in young and open forest, by the mires.
        base =
          smooth01 (0.2f, 0.45f, p.forest.numerical_value_in (one)) *
          (1.0f -
           0.3f * smooth01 (0.8f, 1.0f, p.forest.numerical_value_in (one))) *
          (0.6f +
           0.4f * smooth01 (0.0f, 0.3f, p.bog.numerical_value_in (one))) *
          (whole - p.fell);
        break;
      case Species::roe_deer:
        // The edges: half in the open, half under the trees.
        base = quantity_cast<proportion> (
          smooth01 (0.05f, 0.3f, p.forest.numerical_value_in (one)) *
          (1.0f - smooth01 (0.55f, 0.8f, p.forest.numerical_value_in (one))) *
          (whole - 0.8f * p.fell) * (whole - 0.6f * p.bog));
        break;
      case Species::reindeer:
        base = smooth01 (0.25f, 0.6f, p.fell.numerical_value_in (one)) *
               (whole - p.forest);
        break;
      }
      return quantity_cast<proportion> (base * off_road);
    }

    // Brought into the world's one copy, in each lane that wraps.
    plan_position_t wrap_into (plan_position_t at, const AnimalGround& g) {
      Vec2& p = plan_value (at);
      const Vec2& n = plan_value (g.period);
      if (n.x () > 0.0f)
        p[0] = terrain::wrap_coordinate (p.x (), n.x ());
      if (n.z () > 0.0f)
        p[1] = terrain::wrap_coordinate (p.z (), n.z ());
      return at;
    }

    position_t wrap_into (position_t at, const AnimalGround& g) {
      Vec3& p = position_value (at);
      const Vec2& n = plan_value (g.period);
      if (n.x () > 0.0f)
        p[0] = terrain::wrap_coordinate (p[0], n.x ());
      if (n.z () > 0.0f)
        p[2] = terrain::wrap_coordinate (p[2], n.z ());
      return at;
    }

    // Set down on the ground under it.
    position_t on_ground (const plan_position_t& at, const AnimalGround& g) {
      return terrain::at_elevation (at, g.ground (at));
    }

    // Lays out a herd of `count` round `home`, each where it can stand.
    void gather_herd (AnimalPlan& plan,
                      const AnimalGround& g,
                      Species species,
                      std::uint32_t herd,
                      int count,
                      std::uint32_t seed) {
      const SpeciesTraits& t = traits (species);
      const plan_position_t home = moppe::plan (plan.herds[herd].home);
      int placed = 0;
      for (int tries = 0; placed < count && tries < count * 12; ++tries) {
        const std::uint32_t h = animals_hash (seed, tries + 1);
        const radians_t angle = animals_tau * animals_unit (h, 1) * u::rad;
        const meters_t r = t.length *
                           (0.8f + 3.5f * std::sqrt (animals_unit (h, 2))) *
                           (1.0f + 0.25f * count);
        const float turn = radians_value (angle), rv = meters_value (r);
        const plan_position_t at =
          wrap_into (home + plan_displacement (Vec2 (std::cos (turn) * rv,
                                                     std::sin (turn) * rv)),
                     g);
        if (!footing (g, at) || g.pasture (at).road > 0.05f * proportion[one])
          continue;
        bool crowded = false;
        for (std::size_t k = plan.animals.size () - placed;
             k < plan.animals.size ();
             ++k)
          crowded |=
            flat_distance (moppe::plan (plan.animals[k].position), at, g) <
            t.length * 0.9f;
        if (crowded)
          continue;
        Animal a;
        a.species = species;
        a.seed = animals_hash (h, 3);
        a.herd = herd;
        a.position = on_ground (at, g);
        a.heading = animals_tau * animals_unit (h, 4) * u::rad;
        a.doing = Animal::Doing::grazing;
        a.left = (4.0f + 20.0f * animals_unit (h, 5)) * u::s;
        a.head = 1.0f * head_carriage[one];
        a.goal = moppe::plan (a.position);
        plan.animals.push_back (a);
        ++placed;
      }
    }
  }

  const SpeciesTraits& traits (Species species) {
    return species_traits[static_cast<std::size_t> (species)];
  }

  AnimalPlan plan_animals (const AnimalGround& g,
                           std::span<const Fold> folds,
                           const position_t& trailhead,
                           const plan_extent_t& world,
                           std::uint32_t seed,
                           std::span<const Stall> stalls) {
    AnimalPlan plan;
    // The barns' stock first, each stall or pen a herd of its own.
    for (std::size_t k = 0; k < stalls.size (); ++k) {
      const Stall& stall = stalls[k];
      const std::uint32_t herd =
        static_cast<std::uint32_t> (plan.herds.size ());
      plan.herds.push_back ({ .species = stall.species,
                              .home = stall.at,
                              .range = 1.0f * u::m + stall.half_along });
      const Vec3 along = direction_value (forward_of (stall.heading));
      const Vec3 across (along[2], 0.0f, -along[0]);
      for (int n = 0; n < stall.count; ++n) {
        const std::uint32_t h = animals_hash (
          seed ^ 0xba5eu, static_cast<std::uint32_t> (k * 16 + n));
        Animal a;
        a.species = stall.species;
        a.seed = h;
        a.herd = herd;
        a.indoors = true;
        a.tied = stall.half_along <= meters_t {};
        a.pen = stall.at;
        a.pen_heading = stall.heading;
        a.pen_along = stall.half_along;
        a.pen_across = stall.half_across;
        a.position = stall.at;
        a.heading = stall.heading;
        if (!a.tied) {
          // Spread through the pen, each its own way.
          const float u = (n + 0.5f) / stall.count;
          a.position =
            position (position_value (stall.at) +
                      along * ((2.0f * animals_unit (h, 1) - 1.0f) * 0.6f *
                               meters_value (stall.half_along)) +
                      across * ((2.0f * u - 1.0f) * 0.7f *
                                meters_value (stall.half_across)));
          a.heading = animals_tau * animals_unit (h, 2) * u::rad;
        }
        a.doing = Animal::Doing::grazing;
        a.left = (3.0f + 12.0f * animals_unit (h, 3)) * u::s;
        a.head = 1.0f * head_carriage[one];
        a.goal = moppe::plan (a.position);
        plan.animals.push_back (a);
      }
    }
    const auto add_herd = [&] (Species species,
                               const plan_position_t& home,
                               meters_t range,
                               int count,
                               std::uint32_t herd_seed) {
      plan.herds.push_back (
        { .species = species, .home = on_ground (home, g), .range = range });
      const std::size_t before = plan.animals.size ();
      gather_herd (plan,
                   g,
                   species,
                   static_cast<std::uint32_t> (plan.herds.size () - 1),
                   count,
                   herd_seed);
      if (plan.animals.size () == before)
        plan.herds.pop_back ();
    };

    // The stock grazes the best of the open, grazed ground round each
    // place still lived in, each kind on its own patch of it.
    for (std::size_t f = 0; f < folds.size (); ++f) {
      const Fold& fold = folds[f];
      struct Stock {
        Species species;
        int count;
      };
      std::vector<Stock> stock;
      meters_t reach = 260.0f * u::m;
      switch (fold.kind) {
      case Fold::Kind::farm:
        stock = { { Species::horse, 2 },
                  { Species::cow, 5 },
                  { Species::sheep, 9 } };
        break;
      case Fold::Kind::landing:
        stock = { { Species::cow, 4 },
                  { Species::horse, 1 },
                  { Species::sheep, 7 } };
        break;
      case Fold::Kind::trailhead:
        // A few beasts in sight of where the walk begins.
        stock = { { Species::horse, 2 },
                  { Species::cow, 3 },
                  { Species::sheep, 6 } };
        reach = 160.0f * u::m;
        break;
      case Fold::Kind::sanctuary:
        // The upland summer grazing.
        stock = { { Species::sheep, 11 } };
        reach = 600.0f * u::m;
        break;
      }
      std::vector<plan_position_t> taken;
      for (std::size_t k = 0; k < stock.size (); ++k) {
        const std::uint32_t h =
          animals_hash (seed ^ 0x5f0cu, static_cast<std::uint32_t> (f * 8 + k));
        plan_position_t best = moppe::plan (fold.at);
        float best_score = -1.0f;
        for (int i = 0; i < 160; ++i) {
          const radians_t angle =
            animals_tau * animals_unit (h, 2 * i) * u::rad;
          const meters_t r =
            25.0f * u::m +
            (reach - 25.0f * u::m) * std::sqrt (animals_unit (h, 2 * i + 1));
          const float turn = radians_value (angle), rv = meters_value (r);
          const plan_position_t at =
            wrap_into (moppe::plan (fold.at) +
                         plan_displacement (
                           Vec2 (std::cos (turn) * rv, std::sin (turn) * rv)),
                       g);
          if (!footing (g, at))
            continue;
          const Pasture p = g.pasture (at);
          if (p.road > 0.05f * proportion[one])
            continue;
          float score =
            (liking (stock[k].species, p) * (1.0f - 0.4f * r / reach))
              .numerical_value_in (one);
          // Each kind keeps a little apart from the others.
          for (const plan_position_t& other : taken)
            score *= smooth01 (
              20.0f * u::m, 60.0f * u::m, flat_distance (at, other, g));
          if (score > best_score) {
            best_score = score;
            best = at;
          }
        }
        if (best_score <= 0.02f)
          continue;
        taken.push_back (best);
        add_herd (stock[k].species,
                  best,
                  (fold.kind == Fold::Kind::sanctuary ? 220.0f : 120.0f) * u::m,
                  stock[k].count,
                  animals_hash (h, 999));
      }
    }

    // The wild ones: a few of the cells of a coarse grid over the world
    // hold a group of whichever kind its ground suits best -- moose and
    // roe deer more often near the trailhead, so that the first walk
    // meets some -- and the reindeer keep to the highest of the fell.
    constexpr meters_t cell = 280.0f * u::m;
    const int nx = std::max (
      1, static_cast<int> (plan_value (world).x () / meters_value (cell)));
    const int nz = std::max (
      1, static_cast<int> (plan_value (world).z () / meters_value (cell)));
    struct Candidate {
      Species species;
      plan_position_t at;
      // Its liking for the spot times the ground's height there.
      float score;
      std::uint32_t seed;
    };
    std::vector<Candidate> fell;
    for (int j = 0; j < nz; ++j)
      for (int i = 0; i < nx; ++i) {
        const std::uint32_t h = animals_hash (
          seed ^ 0xa11ce5u, static_cast<std::uint32_t> (j * nx + i));
        // The best of a few spots in the cell.
        Species species = Species::moose;
        proportion_t best_score = 0.0f * proportion[one];
        plan_position_t best {};
        for (int s = 0; s < 6; ++s) {
          const plan_position_t at =
            plan_position ((i + animals_unit (h, 10 + 2 * s)) * cell,
                           (j + animals_unit (h, 11 + 2 * s)) * cell);
          if (!footing (g, at))
            continue;
          const Pasture p = g.pasture (at);
          if (p.grazed > 0.3f * proportion[one] ||
              p.road > 0.05f * proportion[one])
            continue;
          for (const Species kind :
               { Species::moose, Species::roe_deer, Species::reindeer }) {
            const proportion_t score = liking (kind, p);
            if (score > best_score) {
              best_score = score;
              species = kind;
              best = at;
            }
          }
        }
        if (best_score < 0.25f * proportion[one])
          continue;
        if (species == Species::reindeer) {
          fell.push_back (
            { species,
              best,
              best_score.numerical_value_in (one) *
                terrain::surface_elevation_value (g.ground (best)),
              animals_hash (h, 3) });
          continue;
        }
        const proportion_t near =
          (1.0f - smooth01 (500.0f * u::m,
                            1600.0f * u::m,
                            flat_distance (best, moppe::plan (trailhead), g))) *
          proportion[one];
        const probability_t odds =
          0.14f * (1.0f + 4.0f * near.numerical_value_in (one)) *
          best_score.numerical_value_in (one) * probability[one];
        if (animals_unit (h, 1) * probability[one] >= odds)
          continue;
        const float u = animals_unit (h, 2);
        const int count =
          species == Species::moose ? 1 + int (u * 2.0f) : 1 + int (u * 3.0f);
        add_herd (species,
                  best,
                  (species == Species::moose ? 350.0f : 220.0f) * u::m,
                  count,
                  animals_hash (h, 3));
      }
    // A few herds of reindeer, on the highest of their ground, apart.
    std::ranges::sort (fell, std::greater {}, &Candidate::score);
    std::vector<plan_position_t> herded;
    for (const Candidate& c : fell) {
      if (herded.size () >= 5)
        break;
      bool apart = true;
      for (const plan_position_t& other : herded)
        apart &= flat_distance (c.at, other, g) > 900.0f * u::m;
      if (!apart)
        continue;
      herded.push_back (c.at);
      add_herd (Species::reindeer,
                c.at,
                450.0f * u::m,
                5 + static_cast<int> (animals_unit (c.seed, 2) * 6.0f),
                c.seed);
    }

    int counts[species_count] = {};
    for (const Animal& a : plan.animals)
      ++counts[static_cast<int> (a.species)];
    std::cerr << "moppe: animals:";
    for (int s = 0; s < species_count; ++s)
      std::cerr << ' ' << counts[s] << ' '
                << traits (static_cast<Species> (s)).plural
                << (s + 1 < species_count ? "," : "");
    std::cerr << ", in " << plan.herds.size () << " herds" << std::endl;
    return plan;
  }

  void Animals::rebuild (AnimalPlan plan) {
    m_plan = std::move (plan);
    m_random.resize (m_plan.animals.size ());
    for (std::size_t i = 0; i < m_random.size (); ++i)
      m_random[i] = m_plan.animals[i].seed | 1u;
  }

  float Animals::rand (std::uint32_t& state) const {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float> (state >> 8) * (1.0f / 16777216.0f);
  }

  // Grass ahead: of a few spots a short walk on, roughly the way it
  // faces, the one it likes best, keeping near its herd and its range.
  void Animals::choose (Animal& a,
                        const Herd& herd,
                        const AnimalGround& g,
                        const position_t& mates) {
    const SpeciesTraits& t = traits (a.species);
    std::uint32_t& r = m_random[&a - m_plan.animals.data ()];
    const meters_t far = (t.stock ? 14.0f : 30.0f) * u::m;
    float best_score = -1e9f;
    plan_position_t best = plan (a.position);
    for (int k = 0; k < 10; ++k) {
      const radians_t turn = (rand (r) + rand (r) - 1.0f) * 2.2f * u::rad;
      const meters_t distance = t.length * 2.0f + far * rand (r);
      const heading_t heading = a.heading + turn;
      const plan_position_t at = wrap_into (
        plan (a.position) + along (plan_forward_of (heading), distance), g);
      const plan_position_t mid = wrap_into (
        plan (a.position) + along (plan_forward_of (heading), 0.5f * distance),
        g);
      if (!footing (g, at) || !g.dry (mid))
        continue;
      const Pasture p = g.pasture (at);
      float score = liking (a.species, p).numerical_value_in (one);
      // Animals may cross a road on the way to fresh grass, but never pick
      // the carriageway or its shoulder as a place to stand and graze.
      score -= 4.0f * p.road.numerical_value_in (one);
      const meters_t from_home = flat_distance (at, plan (herd.home), g);
      const meters_t range = herd.range;
      score -= 2.0f * smooth01 (0.7f * range, range, from_home);
      const meters_t from_mates = flat_distance (at, plan (mates), g);
      const meters_t length = t.length;
      score -= 0.8f * smooth01 (length * 4.0f, length * 12.0f, from_mates);
      score += 0.15f * rand (r);
      if (score > best_score) {
        best_score = score;
        best = at;
      }
    }
    a.goal = best;
  }

  void
  Animals::startle (Animal& a, const position_t& from, const AnimalGround& g) {
    const SpeciesTraits& t = traits (a.species);
    std::uint32_t& r = m_random[&a - m_plan.animals.data ()];
    a.threat = from;
    a.doing = Animal::Doing::fleeing;
    a.fright = 1.0f * proportion[one];
    const plan_displacement_t away =
      wrapped (plan (from), plan (a.position), g);
    const heading_t base =
      length2 (plan_value (away)) > 1e-4f ? heading_of (away) : a.heading;
    // Away, but where the ground lets it go: try straight away first and
    // then further round to either side, preferring off-road ground.
    std::optional<plan_position_t> fallback;
    for (int k = 0; k < 9; ++k) {
      const radians_t swing = ((k % 2 ? 1.0f : -1.0f) * 0.35f * ((k + 1) / 2) +
                               0.3f * (rand (r) - 0.5f)) *
                              u::rad;
      const meters_t distance = t.flight * (0.7f + 0.6f * rand (r));
      const plan_position_t at = wrap_into (
        plan (a.position) + along (plan_forward_of (base + swing), distance),
        g);
      const plan_position_t mid =
        wrap_into (plan (a.position) +
                     along (plan_forward_of (base + swing), 0.5f * distance),
                   g);
      if (footing (g, at) && g.dry (mid)) {
        if (g.pasture (at).road < 0.2f * proportion[one]) {
          a.goal = at;
          a.left = 30.0f * u::s;
          return;
        }
        if (!fallback)
          fallback = at;
      }
    }
    if (fallback) {
      a.goal = *fallback;
      a.left = 30.0f * u::s;
      return;
    }
    // Hemmed in: it stands its ground and watches.
    a.doing = Animal::Doing::alert;
    a.left = 3.0f * u::s;
  }

  // A beast in a barn eats at the manger (or roots about its pen), stands
  // and looks about, and looks round at whoever comes near -- but it
  // stays where it is kept.
  void Animals::keep_indoors (Animal& a,
                              const AnimalGround& g,
                              const Presence* nearest,
                              meters_t nearest_distance,
                              seconds_t dt) {
    // The head, the look, and the heading turn by rates per second.
    const float h = seconds_value (dt);
    const SpeciesTraits& t = traits (a.species);
    std::uint32_t& r = m_random[&a - m_plan.animals.data ()];
    using Doing = Animal::Doing;
    const Vec3 along = direction_value (forward_of (a.pen_heading));
    const Vec3 across (along[2], 0.0f, -along[0]);
    // Someone coming close is looked at, and for a while after.
    const bool watched = nearest && nearest_distance < 6.0f * u::m;
    if (watched && a.doing != Doing::alert && rand (r) < h * 0.8f) {
      a.doing = Doing::alert;
      a.left = (2.0f + 4.0f * rand (r)) * u::s;
    }
    if (a.left < seconds_t {}) {
      const float u = rand (r);
      if (!a.tied && u < 0.35f) {
        // A few steps to another corner of the pen.
        a.goal = plan (position (position_value (a.pen) +
                                 along * ((2.0f * rand (r) - 1.0f) * 0.8f *
                                          meters_value (a.pen_along)) +
                                 across * ((2.0f * rand (r) - 1.0f) * 0.8f *
                                           meters_value (a.pen_across))));
        a.doing = Doing::walking;
        a.left = 12.0f * u::s;
      } else if (u < 0.75f) {
        a.doing = Doing::grazing;
        a.left = (6.0f + 18.0f * rand (r)) * u::s;
      } else {
        a.doing = Doing::standing;
        a.left = (3.0f + 6.0f * rand (r)) * u::s;
      }
    }
    head_carriage_t head = 0.0f * head_carriage[one];
    heading_t face = a.heading;
    speed_t speed {};
    a.look_goal = 0.0f * u::rad;
    switch (a.doing) {
    case Doing::grazing:
      head = 1.0f * head_carriage[one];
      break;
    case Doing::standing:
      head = 0.05f * head_carriage[one];
      a.look_goal = 0.6f *
                    std::sin (seconds_value (a.left) * 0.8f + (a.seed & 63)) *
                    u::rad;
      break;
    case Doing::alert:
    case Doing::fleeing:
      head = -0.2f * head_carriage[one];
      if (nearest) {
        const plan_displacement_t to =
          wrapped (plan (a.position), plan (nearest->position), g);
        const radians_t off = angle_between (a.heading, heading_of (to));
        a.look_goal = std::clamp (off, -1.1f * u::rad, 1.1f * u::rad);
        // Loose, it turns to face them.
        if (!a.tied && abs (off) > 1.0f * u::rad)
          face = heading_of (to);
      }
      if (!watched && a.left < seconds_t {})
        a.left = 0.0f * u::s;
      break;
    case Doing::walking: {
      const plan_displacement_t to = wrapped (plan (a.position), a.goal, g);
      if (flat_length (to) < 0.3f * u::m) {
        a.doing = Doing::grazing;
        a.left = (5.0f + 12.0f * rand (r)) * u::s;
      } else {
        face = heading_of (to);
        speed = t.walk;
      }
      head = 0.3f * head_carriage[one];
      break;
    }
    }
    if (a.tied) {
      a.heading = a.pen_heading;
      a.speed = {};
    } else {
      const radians_t off = angle_between (a.heading, face);
      a.heading += std::clamp (off, -1.0f * h * u::rad, 1.0f * h * u::rad);
      speed *= std::clamp (1.4f - std::abs (radians_value (off)), 0.1f, 1.0f);
      a.speed += std::clamp<speed_t> (speed - a.speed,
                                      -2.0f * u::m / pow<2> (u::s) * dt,
                                      1.0f * u::m / pow<2> (u::s) * dt);
      position_t next = offset (
        a.position, moppe::along (forward_of (a.heading), a.speed * dt));
      // Within the pen's walls, its body's half length in from them.
      const Vec3 rel = position_value (next) - position_value (a.pen);
      const meters_t x =
        std::clamp (dot (rel, along) * u::m,
                    -std::max (a.pen_along - 0.4f * u::m, meters_t {}),
                    std::max (a.pen_along - 0.4f * u::m, meters_t {}));
      const meters_t z =
        std::clamp (dot (rel, across) * u::m,
                    -std::max (a.pen_across - 0.35f * u::m, meters_t {}),
                    std::max (a.pen_across - 0.35f * u::m, meters_t {}));
      next = position (position_value (a.pen) + along * meters_value (x) +
                       across * meters_value (z));
      // Penmates keep their room.
      for (const Animal& other : m_plan.animals)
        if (&other != &a && other.herd == a.herd) {
          const plan_displacement_t d =
            wrapped (plan (other.position), plan (next), g);
          const meters_t l = flat_length (d);
          if (l < 0.75f * u::m && l > 1e-3f * u::m)
            next = position (
              position_value (next) +
              lift (plan_value (d)) *
                ((0.75f * u::m - l) / l * 0.3f).numerical_value_in (one));
        }
      a.gait += length (position_value (next) - position_value (a.position)) /
                meters_value (t.stride) * isq::rotation[one];
      a.position = next;
    }
    a.head += std::clamp (head - a.head,
                          -1.4f * h * head_carriage[one],
                          1.0f * h * head_carriage[one]);
    a.look +=
      std::clamp (a.look_goal - a.look, -1.2f * h * u::rad, 1.2f * h * u::rad);
    a.position = on_ground (plan (a.position), g);
    a.gait = fmod (a.gait, 1024.0f * isq::rotation[one]);
  }

  void Animals::step (const AnimalGround& g,
                      std::span<const Presence> presences,
                      const position_t& focus,
                      meters_t reach,
                      seconds_t dt) {
    if (dt <= seconds_t {})
      return;
    // The head, the look, and the heading turn by rates per second.
    const float h = seconds_value (dt);
    std::vector<Animal>& all = m_plan.animals;
    // The herds' middles, for keeping together.
    std::vector<position_t> middles (m_plan.herds.size ());
    std::vector<int> members (m_plan.herds.size (), 0);
    for (const Animal& a : all) {
      position_t& m = middles[a.herd];
      const int n = members[a.herd]++;
      if (n == 0)
        m = a.position;
      else
        m = position (position_value (m) + lift (plan_value (wrapped (
                                             plan (m), plan (a.position), g))) *
                                             (1.0f / (n + 1)));
    }

    for (std::size_t i = 0; i < all.size (); ++i) {
      Animal& a = all[i];
      if (flat_distance (plan (a.position), plan (focus), g) > reach)
        continue;
      const SpeciesTraits& t = traits (a.species);
      const Herd& herd = m_plan.herds[a.herd];
      std::uint32_t& r = m_random[i];
      a.left -= dt;
      a.fright = std::max (0.0f * proportion[one],
                           a.fright - h / 25.0f * proportion[one]);
      if (a.indoors) {
        const Presence* nearest = nullptr;
        meters_t nearest_distance = 1e9f * u::m;
        for (const Presence& p : presences) {
          const meters_t d =
            flat_distance (plan (a.position), plan (p.position), g);
          if (d < nearest_distance) {
            nearest_distance = d;
            nearest = &p;
          }
        }
        keep_indoors (a, g, nearest, nearest_distance, dt);
        continue;
      }

      // Whoever comes near. Someone moving fast, or a machine, is minded
      // from further off; so is anyone, once it has been frightened.
      const Presence* nearest = nullptr;
      magnitude_t nearest_ratio = 1e9f * one;
      for (const Presence& p : presences) {
        const speed_t speed = std::hypot (velocity_value (p.velocity)[0],
                                          velocity_value (p.velocity)[2]) *
                              u::m / u::s;
        const meters_t wary =
          t.wary * (1.0f + 0.5f * a.fright.numerical_value_in (one)) *
          (p.machine ? 2.2f : 1.0f) *
          (1.0f + std::min (speed_value (speed), 12.0f) / 5.0f);
        const magnitude_t ratio =
          flat_distance (plan (a.position), plan (p.position), g) / wary;
        if (ratio < nearest_ratio) {
          nearest_ratio = ratio;
          nearest = &p;
        }
      }
      if (nearest) {
        // Nothing runs it down: what reaches its body shoves it aside.
        const meters_t room = 0.45f * t.length + 0.6f * u::m;
        const plan_displacement_t off =
          wrapped (plan (nearest->position), plan (a.position), g);
        const meters_t d = flat_length (off);
        if (d < room) {
          const Vec3 out =
            d > 1e-3f * u::m
              ? lift (plan_value (off)) * (1.0f / meters_value (d))
              : direction_value (forward_of (a.heading));
          a.position = position (position_value (a.position) +
                                 out * meters_value (room - d));
          if (a.doing != Animal::Doing::fleeing)
            startle (a, nearest->position, g);
        }
      }
      if (nearest && nearest_ratio < 1.0f * one &&
          a.doing != Animal::Doing::fleeing) {
        const bool close =
          nearest_ratio < 0.55f * one || nearest->machine ||
          (a.doing == Animal::Doing::alert && a.left < seconds_t {});
        if (close) {
          startle (a, nearest->position, g);
          // The rest of the herd goes too.
          for (Animal& mate : all)
            if (&mate != &a && mate.herd == a.herd &&
                mate.doing != Animal::Doing::fleeing &&
                flat_distance (plan (mate.position), plan (a.position), g) <
                  (t.stock ? 25.0f : 60.0f) * u::m)
              startle (mate, nearest->position, g);
        } else if (a.doing != Animal::Doing::alert) {
          a.doing = Animal::Doing::alert;
          a.threat = nearest->position;
          a.left = (1.2f + 1.8f * rand (r)) * u::s;
        }
      }
      if (nearest && a.doing == Animal::Doing::alert)
        a.threat = nearest->position;

      // What next, once what it was doing is done.
      using Doing = Animal::Doing;
      const bool there =
        flat_distance (plan (a.position), a.goal, g) < 0.8f * u::m;
      const bool on_road =
        g.pasture (plan (a.position)).road > 0.25f * proportion[one];
      switch (a.doing) {
      case Doing::grazing:
        if (on_road) {
          choose (a, herd, g, middles[a.herd]);
          a.doing = Doing::walking;
          a.left = 40.0f * u::s;
        } else if (a.left < seconds_t {}) {
          const float u = rand (r);
          if (u < 0.55f) {
            choose (a, herd, g, middles[a.herd]);
            a.doing = Doing::walking;
            a.left = 40.0f * u::s;
          } else if (u < 0.85f) {
            a.doing = Doing::standing;
            a.left = (3.0f + 7.0f * rand (r)) * u::s;
          } else {
            a.left = (6.0f + 12.0f * rand (r)) * u::s;
          }
        }
        break;
      case Doing::standing:
        if (on_road) {
          choose (a, herd, g, middles[a.herd]);
          a.doing = Doing::walking;
          a.left = 40.0f * u::s;
        } else if (a.left < seconds_t {}) {
          if (rand (r) < 0.6f) {
            a.doing = Doing::grazing;
            a.left = (8.0f + 22.0f * rand (r)) * u::s;
          } else {
            choose (a, herd, g, middles[a.herd]);
            a.doing = Doing::walking;
            a.left = 40.0f * u::s;
          }
        }
        break;
      case Doing::walking:
        if (there || a.left < seconds_t {}) {
          if (on_road) {
            choose (a, herd, g, middles[a.herd]);
            a.left = 40.0f * u::s;
          } else {
            a.doing = Doing::grazing;
            a.left = (8.0f + 22.0f * rand (r)) * u::s;
          }
        }
        break;
      case Doing::alert:
        if (a.left < seconds_t {} && (!nearest || nearest_ratio > 1.0f * one)) {
          a.doing = Doing::standing;
          a.left = (2.0f + 4.0f * rand (r)) * u::s;
        }
        break;
      case Doing::fleeing:
        if (there || a.left < seconds_t {}) {
          // Stopped, it looks back at what it ran from.
          a.doing = Doing::standing;
          a.left = (4.0f + 6.0f * rand (r)) * u::s;
        }
        break;
      }

      // Where it wants to face and how fast it wants to go.
      speed_t speed {};
      heading_t face = a.heading;
      radians_per_second_t turn_rate = 1.2f * u::rad / u::s;
      head_carriage_t head = 0.0f * head_carriage[one];
      a.look_goal = 0.0f * u::rad;
      switch (a.doing) {
      case Doing::grazing: {
        head = 1.0f * head_carriage[one];
        // Now and then a step or two on to the next mouthful.
        const proportion_t beat =
          std::fmod (seconds_value (a.left) * 0.23f + (a.seed & 255) / 255.0f,
                     1.0f) *
          proportion[one];
        speed = beat < 0.16f * proportion[one] ? 0.35f * t.walk : speed_t {};
        turn_rate = 0.3f * u::rad / u::s;
        face = a.heading + 0.3f *
                             std::sin (seconds_value (a.left) * 0.4f + a.seed) *
                             u::rad;
        break;
      }
      case Doing::standing: {
        head = 0.05f * head_carriage[one];
        if (length2 (position_value (a.threat)) > 0.0f &&
            a.fright > 0.2f * proportion[one]) {
          const plan_displacement_t to =
            wrapped (plan (a.position), plan (a.threat), g);
          a.look_goal = std::clamp (angle_between (a.heading, heading_of (to)),
                                    -1.0f * u::rad,
                                    1.0f * u::rad);
        } else {
          a.look_goal =
            0.7f * std::sin (seconds_value (a.left) * 0.9f + (a.seed & 63)) *
            u::rad;
        }
        break;
      }
      case Doing::walking: {
        const plan_displacement_t to = wrapped (plan (a.position), a.goal, g);
        face = heading_of (to);
        speed = t.walk;
        head = 0.2f * head_carriage[one];
        break;
      }
      case Doing::alert: {
        const plan_displacement_t to =
          wrapped (plan (a.position), plan (a.threat), g);
        const heading_t toward = heading_of (to);
        // It turns its head first and then its body if it must.
        a.look_goal = std::clamp (
          angle_between (a.heading, toward), -0.9f * u::rad, 0.9f * u::rad);
        if (abs (angle_between (a.heading, toward)) > 1.2f * u::rad)
          face = toward;
        turn_rate = 1.5f * u::rad / u::s;
        head = -0.4f * head_carriage[one];
        break;
      }
      case Doing::fleeing: {
        const plan_displacement_t to = wrapped (plan (a.position), a.goal, g);
        face = heading_of (to);
        speed =
          t.stock && a.fright < 0.5f * proportion[one] ? t.walk * 1.6f : t.run;
        // Slowing as it draws away.
        speed *=
          0.4f + 0.6f * smooth01 (0.0f, 8.0f, meters_value (flat_length (to)));
        turn_rate = 3.0f * u::rad / u::s;
        head = 0.0f * head_carriage[one];
        break;
      }
      }

      const radians_t off = angle_between (a.heading, face);
      a.heading += std::clamp<radians_t> (off, -turn_rate * dt, turn_rate * dt);
      // Turning hard, it slows to turn on the spot.
      speed *= std::clamp (1.4f - std::abs (radians_value (off)), 0.15f, 1.0f);
      const metres_per_second_squared_t accel =
        (a.doing == Doing::fleeing ? 6.0f : 1.5f) * u::m / pow<2> (u::s);
      a.speed +=
        std::clamp<speed_t> (speed - a.speed, -accel * dt * 1.5f, accel * dt);
      a.head += std::clamp (head - a.head,
                            -1.6f * h * head_carriage[one],
                            1.2f * h * head_carriage[one]);
      a.look += std::clamp (
        a.look_goal - a.look, -1.5f * h * u::rad, 1.5f * h * u::rad);

      if (a.speed > 1e-3f * u::m / u::s) {
        position_t next =
          offset (a.position, along (forward_of (a.heading), a.speed * dt));
        next = wrap_into (next, g);
        // Herd mates keep their room.
        for (std::size_t k = 0; k < all.size (); ++k) {
          if (k == i || all[k].herd != a.herd)
            continue;
          const plan_displacement_t d =
            wrapped (plan (all[k].position), plan (next), g);
          const meters_t l = flat_length (d);
          const meters_t room = 0.8f * t.length;
          if (l < room && l > 1e-3f * u::m)
            next =
              position (position_value (next) +
                        lift (plan_value (d)) *
                          ((room - l) / l * 0.5f).numerical_value_in (one));
        }
        if (g.clear)
          next = g.clear (next, 0.3f * t.length);
        if (footing (g, plan (next))) {
          a.position = next;
          const meters_t stride =
            a.speed > 1.6f * t.walk ? 2.4f * t.stride : t.stride;
          a.gait += (a.speed * dt / stride).numerical_value_in (one) *
                    isq::rotation[one];
        } else {
          // Water or a crag ahead: it stops and thinks again.
          a.speed = {};
          a.goal = plan (a.position);
          if (a.doing == Doing::fleeing)
            a.doing = Doing::alert, a.left = 2.0f * u::s;
        }
      }
      a.position = on_ground (plan (a.position), g);
      a.gait = fmod (a.gait, 1024.0f * isq::rotation[one]);
    }
  }
}
