#include <moppe/game/animals.hh>

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
        .walk = 1.5f,
        .run = 4.5f,
        .wary = 5.0f,
        .flight = 25.0f,
        .length = 1.7f,
        .stride = 1.7f,
        .stock = true },
      { .name = "cow",
        .plural = "cows",
        .walk = 1.1f,
        .run = 3.0f,
        .wary = 5.0f,
        .flight = 18.0f,
        .length = 1.6f,
        .stride = 1.4f,
        .stock = true },
      { .name = "sheep",
        .plural = "sheep",
        .walk = 0.9f,
        .run = 3.2f,
        .wary = 9.0f,
        .flight = 22.0f,
        .length = 0.95f,
        .stride = 0.75f,
        .stock = true },
      { .name = "reindeer",
        .plural = "reindeer",
        .walk = 1.4f,
        .run = 6.0f,
        .wary = 30.0f,
        .flight = 70.0f,
        .length = 1.3f,
        .stride = 1.4f },
      { .name = "moose",
        .plural = "moose",
        .walk = 1.5f,
        .run = 6.5f,
        .wary = 28.0f,
        .flight = 80.0f,
        .length = 1.9f,
        .stride = 2.0f },
      { .name = "roe deer",
        .plural = "roe deer",
        .walk = 1.0f,
        .run = 8.0f,
        .wary = 38.0f,
        .flight = 90.0f,
        .length = 0.9f,
        .stride = 0.9f,
        .bounds = true },
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

    // The shortest way from `from` to `to` in a periodic world.
    Vec3 wrapped (const Vec3& from, const Vec3& to, const AnimalGround& g) {
      Vec3 d = to - from;
      d[1] = 0.0f;
      if (g.period_x > 0.0f)
        d[0] -= g.period_x * std::round (d[0] / g.period_x);
      if (g.period_z > 0.0f)
        d[2] -= g.period_z * std::round (d[2] / g.period_z);
      return d;
    }

    float flat_distance (const Vec3& a, const Vec3& b, const AnimalGround& g) {
      const Vec3 d = wrapped (a, b, g);
      return std::hypot (d[0], d[2]);
    }

    float heading_of (const Vec3& d) {
      return std::atan2 (d[0], d[2]);
    }

    Vec3 forward_of (float heading) {
      return Vec3 (std::sin (heading), 0.0f, std::cos (heading));
    }

    float angle_between (float a, float b) {
      float d = std::remainder (b - a, animals_tau);
      return d;
    }

    // How steep the ground is about a point, rise over run.
    float steepness (const AnimalGround& g, float x, float z) {
      const float e = 2.0f;
      const float dx = g.ground (x + e, z) - g.ground (x - e, z);
      const float dz = g.ground (x, z + e) - g.ground (x, z - e);
      return std::hypot (dx, dz) / (2.0f * e);
    }

    // Whether an animal can stand at a point.
    bool footing (const AnimalGround& g, float x, float z) {
      return g.dry (x, z) && steepness (g, x, z) < 0.55f;
    }

    // How much a species wants to be at a point, 0..1.
    float liking (Species species, const Pasture& p) {
      switch (species) {
      case Species::horse:
      case Species::cow:
      case Species::sheep:
        return p.grazed * (1.0f - 0.8f * p.forest) * (1.0f - 0.7f * p.bog);
      case Species::moose:
        // Browse in young and open forest, by the mires.
        return smooth01 (0.2f, 0.45f, p.forest) *
               (1.0f - 0.3f * smooth01 (0.8f, 1.0f, p.forest)) *
               (0.6f + 0.4f * smooth01 (0.0f, 0.3f, p.bog)) * (1.0f - p.fell);
      case Species::roe_deer:
        // The edges: half in the open, half under the trees.
        return smooth01 (0.05f, 0.3f, p.forest) *
               (1.0f - smooth01 (0.55f, 0.8f, p.forest)) *
               (1.0f - 0.8f * p.fell) * (1.0f - 0.6f * p.bog);
      case Species::reindeer:
        return smooth01 (0.25f, 0.6f, p.fell) * (1.0f - p.forest);
      }
      return 0.0f;
    }

    Vec3 wrap_into (Vec3 at, const AnimalGround& g) {
      if (g.period_x > 0.0f)
        at[0] -= std::floor (at[0] / g.period_x) * g.period_x;
      if (g.period_z > 0.0f)
        at[2] -= std::floor (at[2] / g.period_z) * g.period_z;
      return at;
    }

    // Lays out a herd of `count` round `home`, each where it can stand.
    void gather_herd (AnimalPlan& plan,
                      const AnimalGround& g,
                      Species species,
                      std::uint32_t herd,
                      int count,
                      std::uint32_t seed) {
      const SpeciesTraits& t = traits (species);
      const Vec3 home = plan.herds[herd].home;
      int placed = 0;
      for (int tries = 0; placed < count && tries < count * 12; ++tries) {
        const std::uint32_t h = animals_hash (seed, tries + 1);
        const float angle = animals_tau * animals_unit (h, 1);
        const float r = t.length *
                        (0.8f + 3.5f * std::sqrt (animals_unit (h, 2))) *
                        (1.0f + 0.25f * count);
        const Vec3 at = wrap_into (
          home + Vec3 (std::cos (angle) * r, 0.0f, std::sin (angle) * r), g);
        if (!footing (g, at[0], at[2]))
          continue;
        bool crowded = false;
        for (std::size_t k = plan.animals.size () - placed;
             k < plan.animals.size ();
             ++k)
          crowded |=
            flat_distance (plan.animals[k].position, at, g) < t.length * 0.9f;
        if (crowded)
          continue;
        Animal a;
        a.species = species;
        a.seed = animals_hash (h, 3);
        a.herd = herd;
        a.position = Vec3 (at[0], g.ground (at[0], at[2]), at[2]);
        a.heading = animals_tau * animals_unit (h, 4);
        a.doing = Animal::Doing::grazing;
        a.left = 4.0f + 20.0f * animals_unit (h, 5);
        a.head = 1.0f;
        a.goal = a.position;
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
                           const Vec3& trailhead,
                           float world_x,
                           float world_z,
                           std::uint32_t seed) {
    AnimalPlan plan;
    const auto add_herd = [&] (Species species,
                               const Vec3& home,
                               float range,
                               int count,
                               std::uint32_t herd_seed) {
      plan.herds.push_back (
        { .species = species,
          .home = Vec3 (home[0], g.ground (home[0], home[2]), home[2]),
          .range = range });
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
      float reach = 260.0f;
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
        reach = 160.0f;
        break;
      case Fold::Kind::sanctuary:
        // The upland summer grazing.
        stock = { { Species::sheep, 11 } };
        reach = 600.0f;
        break;
      }
      std::vector<Vec3> taken;
      for (std::size_t k = 0; k < stock.size (); ++k) {
        const std::uint32_t h =
          animals_hash (seed ^ 0x5f0cu, static_cast<std::uint32_t> (f * 8 + k));
        Vec3 best = fold.at;
        float best_score = -1.0f;
        for (int i = 0; i < 160; ++i) {
          const float angle = animals_tau * animals_unit (h, 2 * i);
          const float r =
            25.0f + (reach - 25.0f) * std::sqrt (animals_unit (h, 2 * i + 1));
          const Vec3 at = wrap_into (
            fold.at + Vec3 (std::cos (angle) * r, 0.0f, std::sin (angle) * r),
            g);
          if (!footing (g, at[0], at[2]))
            continue;
          float score = liking (stock[k].species, g.pasture (at[0], at[2])) *
                        (1.0f - 0.4f * r / reach);
          // Each kind keeps a little apart from the others.
          for (const Vec3& other : taken)
            score *= smooth01 (20.0f, 60.0f, flat_distance (at, other, g));
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
                  fold.kind == Fold::Kind::sanctuary ? 220.0f : 120.0f,
                  stock[k].count,
                  animals_hash (h, 999));
      }
    }

    // The wild ones: a few of the cells of a coarse grid over the world
    // hold a group of whichever kind its ground suits best -- moose and
    // roe deer more often near the trailhead, so that the first walk
    // meets some -- and the reindeer keep to the highest of the fell.
    constexpr float cell = 280.0f;
    const int nx = std::max (1, static_cast<int> (world_x / cell));
    const int nz = std::max (1, static_cast<int> (world_z / cell));
    struct Candidate {
      Species species;
      Vec3 at;
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
        float best_score = 0.0f;
        Vec3 best {};
        for (int s = 0; s < 6; ++s) {
          const Vec3 at ((i + animals_unit (h, 10 + 2 * s)) * cell,
                         0.0f,
                         (j + animals_unit (h, 11 + 2 * s)) * cell);
          if (!footing (g, at[0], at[2]))
            continue;
          const Pasture p = g.pasture (at[0], at[2]);
          if (p.grazed > 0.3f)
            continue;
          for (const Species kind :
               { Species::moose, Species::roe_deer, Species::reindeer }) {
            const float score = liking (kind, p);
            if (score > best_score) {
              best_score = score;
              species = kind;
              best = at;
            }
          }
        }
        if (best_score < 0.25f)
          continue;
        if (species == Species::reindeer) {
          fell.push_back ({ species,
                            best,
                            best_score * g.ground (best[0], best[2]),
                            animals_hash (h, 3) });
          continue;
        }
        const float near =
          1.0f - smooth01 (500.0f, 1600.0f, flat_distance (best, trailhead, g));
        const float odds = 0.14f * (1.0f + 4.0f * near) * best_score;
        if (animals_unit (h, 1) >= odds)
          continue;
        const float u = animals_unit (h, 2);
        const int count =
          species == Species::moose ? 1 + int (u * 2.0f) : 1 + int (u * 3.0f);
        add_herd (species,
                  best,
                  species == Species::moose ? 350.0f : 220.0f,
                  count,
                  animals_hash (h, 3));
      }
    // A few herds of reindeer, on the highest of their ground, apart.
    std::ranges::sort (fell, std::greater {}, &Candidate::score);
    std::vector<Vec3> herded;
    for (const Candidate& c : fell) {
      if (herded.size () >= 5)
        break;
      bool apart = true;
      for (const Vec3& other : herded)
        apart &= flat_distance (c.at, other, g) > 900.0f;
      if (!apart)
        continue;
      herded.push_back (c.at);
      add_herd (Species::reindeer,
                c.at,
                450.0f,
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
                        const Vec3& mates) {
    const SpeciesTraits& t = traits (a.species);
    std::uint32_t& r = m_random[&a - m_plan.animals.data ()];
    const float far = t.stock ? 14.0f : 30.0f;
    float best_score = -1e9f;
    Vec3 best = a.position;
    for (int k = 0; k < 10; ++k) {
      const float turn = (rand (r) + rand (r) - 1.0f) * 2.2f;
      const float distance = t.length * 2.0f + far * rand (r);
      const float heading = a.heading + turn;
      const Vec3 at =
        wrap_into (a.position + forward_of (heading) * distance, g);
      const Vec3 mid =
        wrap_into (a.position + forward_of (heading) * (0.5f * distance), g);
      if (!footing (g, at[0], at[2]) || !g.dry (mid[0], mid[2]))
        continue;
      float score = liking (a.species, g.pasture (at[0], at[2]));
      const float from_home = flat_distance (at, herd.home, g);
      score -= 2.0f * smooth01 (0.7f * herd.range, herd.range, from_home);
      const float from_mates = flat_distance (at, mates, g);
      score -= 0.8f * smooth01 (t.length * 4.0f, t.length * 12.0f, from_mates);
      score += 0.15f * rand (r);
      if (score > best_score) {
        best_score = score;
        best = at;
      }
    }
    a.goal = best;
  }

  void Animals::startle (Animal& a, const Vec3& from, const AnimalGround& g) {
    const SpeciesTraits& t = traits (a.species);
    std::uint32_t& r = m_random[&a - m_plan.animals.data ()];
    a.threat = from;
    a.doing = Animal::Doing::fleeing;
    a.fright = 1.0f;
    const Vec3 away = wrapped (from, a.position, g);
    const float base = length2 (away) > 1e-4f ? heading_of (away) : a.heading;
    // Away, but where the ground lets it go: try straight away first and
    // then further round to either side.
    for (int k = 0; k < 9; ++k) {
      const float swing = (k % 2 ? 1.0f : -1.0f) * 0.35f * ((k + 1) / 2) +
                          0.3f * (rand (r) - 0.5f);
      const float distance = t.flight * (0.7f + 0.6f * rand (r));
      const Vec3 at =
        wrap_into (a.position + forward_of (base + swing) * distance, g);
      const Vec3 mid = wrap_into (
        a.position + forward_of (base + swing) * (0.5f * distance), g);
      if (footing (g, at[0], at[2]) && g.dry (mid[0], mid[2])) {
        a.goal = at;
        a.left = 30.0f;
        return;
      }
    }
    // Hemmed in: it stands its ground and watches.
    a.doing = Animal::Doing::alert;
    a.left = 3.0f;
  }

  void Animals::step (const AnimalGround& g,
                      std::span<const Presence> presences,
                      const Vec3& focus,
                      float reach,
                      float dt) {
    if (dt <= 0.0f)
      return;
    std::vector<Animal>& all = m_plan.animals;
    // The herds' middles, for keeping together.
    std::vector<Vec3> middles (m_plan.herds.size ());
    std::vector<int> members (m_plan.herds.size (), 0);
    for (const Animal& a : all) {
      Vec3& m = middles[a.herd];
      const int n = members[a.herd]++;
      if (n == 0)
        m = a.position;
      else
        m = m + wrapped (m, a.position, g) * (1.0f / (n + 1));
    }

    for (std::size_t i = 0; i < all.size (); ++i) {
      Animal& a = all[i];
      if (flat_distance (a.position, focus, g) > reach)
        continue;
      const SpeciesTraits& t = traits (a.species);
      const Herd& herd = m_plan.herds[a.herd];
      std::uint32_t& r = m_random[i];
      a.left -= dt;
      a.fright = std::max (0.0f, a.fright - dt / 25.0f);

      // Whoever comes near. Someone moving fast, or a machine, is minded
      // from further off; so is anyone, once it has been frightened.
      const Presence* nearest = nullptr;
      float nearest_ratio = 1e9f;
      for (const Presence& p : presences) {
        const float speed = std::hypot (p.velocity[0], p.velocity[2]);
        const float wary = t.wary * (1.0f + 0.5f * a.fright) *
                           (p.machine ? 2.2f : 1.0f) *
                           (1.0f + std::min (speed, 12.0f) / 5.0f);
        const float ratio = flat_distance (a.position, p.position, g) / wary;
        if (ratio < nearest_ratio) {
          nearest_ratio = ratio;
          nearest = &p;
        }
      }
      if (nearest) {
        // Nothing runs it down: what reaches its body shoves it aside.
        const float room = 0.45f * t.length + 0.6f;
        const Vec3 off = wrapped (nearest->position, a.position, g);
        const float d = std::hypot (off[0], off[2]);
        if (d < room) {
          const Vec3 out =
            d > 1e-3f ? off * (1.0f / d) : forward_of (a.heading);
          a.position = a.position + out * (room - d);
          if (a.doing != Animal::Doing::fleeing)
            startle (a, nearest->position, g);
        }
      }
      if (nearest && nearest_ratio < 1.0f &&
          a.doing != Animal::Doing::fleeing) {
        const bool close = nearest_ratio < 0.55f || nearest->machine ||
                           (a.doing == Animal::Doing::alert && a.left < 0.0f);
        if (close) {
          startle (a, nearest->position, g);
          // The rest of the herd goes too.
          for (Animal& mate : all)
            if (&mate != &a && mate.herd == a.herd &&
                mate.doing != Animal::Doing::fleeing &&
                flat_distance (mate.position, a.position, g) <
                  (t.stock ? 25.0f : 60.0f))
              startle (mate, nearest->position, g);
        } else if (a.doing != Animal::Doing::alert) {
          a.doing = Animal::Doing::alert;
          a.threat = nearest->position;
          a.left = 1.2f + 1.8f * rand (r);
        }
      }
      if (nearest && a.doing == Animal::Doing::alert)
        a.threat = nearest->position;

      // What next, once what it was doing is done.
      using Doing = Animal::Doing;
      const bool there = flat_distance (a.position, a.goal, g) < 0.8f;
      switch (a.doing) {
      case Doing::grazing:
        if (a.left < 0.0f) {
          const float u = rand (r);
          if (u < 0.55f) {
            choose (a, herd, g, middles[a.herd]);
            a.doing = Doing::walking;
            a.left = 40.0f;
          } else if (u < 0.85f) {
            a.doing = Doing::standing;
            a.left = 3.0f + 7.0f * rand (r);
          } else {
            a.left = 6.0f + 12.0f * rand (r);
          }
        }
        break;
      case Doing::standing:
        if (a.left < 0.0f) {
          if (rand (r) < 0.6f) {
            a.doing = Doing::grazing;
            a.left = 8.0f + 22.0f * rand (r);
          } else {
            choose (a, herd, g, middles[a.herd]);
            a.doing = Doing::walking;
            a.left = 40.0f;
          }
        }
        break;
      case Doing::walking:
        if (there || a.left < 0.0f) {
          a.doing = Doing::grazing;
          a.left = 8.0f + 22.0f * rand (r);
        }
        break;
      case Doing::alert:
        if (a.left < 0.0f && (!nearest || nearest_ratio > 1.0f)) {
          a.doing = Doing::standing;
          a.left = 2.0f + 4.0f * rand (r);
        }
        break;
      case Doing::fleeing:
        if (there || a.left < 0.0f) {
          // Stopped, it looks back at what it ran from.
          a.doing = Doing::standing;
          a.left = 4.0f + 6.0f * rand (r);
        }
        break;
      }

      // Where it wants to face and how fast it wants to go.
      float speed = 0.0f;
      float face = a.heading;
      float turn_rate = 1.2f;
      float head = 0.0f;
      a.look_goal = 0.0f;
      switch (a.doing) {
      case Doing::grazing: {
        head = 1.0f;
        // Now and then a step or two on to the next mouthful.
        const float beat =
          std::fmod (a.left * 0.23f + (a.seed & 255) / 255.0f, 1.0f);
        speed = beat < 0.16f ? 0.35f * t.walk : 0.0f;
        turn_rate = 0.3f;
        face = a.heading + 0.3f * std::sin (a.left * 0.4f + a.seed);
        break;
      }
      case Doing::standing: {
        head = 0.05f;
        if (length2 (a.threat) > 0.0f && a.fright > 0.2f) {
          const Vec3 to = wrapped (a.position, a.threat, g);
          a.look_goal = std::clamp (
            angle_between (a.heading, heading_of (to)), -1.0f, 1.0f);
        } else {
          a.look_goal = 0.7f * std::sin (a.left * 0.9f + (a.seed & 63));
        }
        break;
      }
      case Doing::walking: {
        const Vec3 to = wrapped (a.position, a.goal, g);
        face = heading_of (to);
        speed = t.walk;
        head = 0.2f;
        break;
      }
      case Doing::alert: {
        const Vec3 to = wrapped (a.position, a.threat, g);
        const float toward = heading_of (to);
        // It turns its head first and then its body if it must.
        a.look_goal =
          std::clamp (angle_between (a.heading, toward), -0.9f, 0.9f);
        if (std::abs (angle_between (a.heading, toward)) > 1.2f)
          face = toward;
        turn_rate = 1.5f;
        head = -0.4f;
        break;
      }
      case Doing::fleeing: {
        const Vec3 to = wrapped (a.position, a.goal, g);
        face = heading_of (to);
        speed = t.stock && a.fright < 0.5f ? t.walk * 1.6f : t.run;
        // Slowing as it draws away.
        speed *= 0.4f + 0.6f * smooth01 (0.0f, 8.0f, std::hypot (to[0], to[2]));
        turn_rate = 3.0f;
        head = 0.0f;
        break;
      }
      }

      const float off = angle_between (a.heading, face);
      a.heading += std::clamp (off, -turn_rate * dt, turn_rate * dt);
      // Turning hard, it slows to turn on the spot.
      speed *= std::clamp (1.4f - std::abs (off), 0.15f, 1.0f);
      const float accel = a.doing == Doing::fleeing ? 6.0f : 1.5f;
      a.speed += std::clamp (speed - a.speed, -accel * dt * 1.5f, accel * dt);
      a.head += std::clamp (head - a.head, -1.6f * dt, 1.2f * dt);
      a.look += std::clamp (a.look_goal - a.look, -1.5f * dt, 1.5f * dt);

      if (a.speed > 1e-3f) {
        Vec3 next = a.position + forward_of (a.heading) * (a.speed * dt);
        next = wrap_into (next, g);
        // Herd mates keep their room.
        for (std::size_t k = 0; k < all.size (); ++k) {
          if (k == i || all[k].herd != a.herd)
            continue;
          const Vec3 d = wrapped (all[k].position, next, g);
          const float l = std::hypot (d[0], d[2]);
          const float room = 0.8f * t.length;
          if (l < room && l > 1e-3f)
            next = next + d * ((room - l) / l * 0.5f);
        }
        if (g.clear)
          next = g.clear (next, 0.3f * t.length);
        if (footing (g, next[0], next[2])) {
          a.position = next;
          const float stride =
            a.speed > 1.6f * t.walk ? 2.4f * t.stride : t.stride;
          a.gait += a.speed * dt / stride;
        } else {
          // Water or a crag ahead: it stops and thinks again.
          a.speed = 0.0f;
          a.goal = a.position;
          if (a.doing == Doing::fleeing)
            a.doing = Doing::alert, a.left = 2.0f;
        }
      }
      a.position[1] = g.ground (a.position[0], a.position[2]);
      a.gait = std::fmod (a.gait, 1024.0f);
    }
  }
}
