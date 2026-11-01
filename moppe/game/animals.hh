#ifndef MOPPE_GAME_ANIMALS_HH
#define MOPPE_GAME_ANIMALS_HH

// The animals of the country at 60 degrees north: the stock of the places
// still lived in -- North Swedish horses, fjällkor, and Gotland sheep --
// out on the ground that is really grazed, and the wild ones where their
// country is: moose in the forest by the mires, roe deer at the forest's
// edges, and reindeer up on the fell. They wander, stop, put their heads
// down and graze, and walk on to fresh grass; someone coming close makes
// them look up and move off, the wild ones further and sooner. Nothing
// here can hurt them: what comes upon one pushes it aside, and it goes.

#include <moppe/gfx/math.hh>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace moppe::game {
  enum class Species : std::uint8_t {
    horse,
    cow,
    sheep,
    reindeer,
    moose,
    roe_deer
  };
  constexpr int species_count = 6;

  // What a species is like to meet.
  struct SpeciesTraits {
    const char* name;
    const char* plural;
    // Walking and running pace, m/s.
    float walk = 1.0f;
    float run = 3.0f;
    // How near someone on foot may come before it moves off, m; anything
    // moving fast is minded from further off.
    float wary = 10.0f;
    // How far it goes once startled, m.
    float flight = 30.0f;
    // Its length from chest to rump, m: its room among its herd.
    float length = 1.6f;
    // How far one stride carries it at a walk, m.
    float stride = 1.6f;
    // Kept stock (as opposed to wild).
    bool stock = false;
    // Runs by bounding (the roe deer) rather than trotting.
    bool bounds = false;
  };
  const SpeciesTraits& traits (Species species);

  // What the ground offers an animal at a point, each 0..1.
  struct Pasture {
    // Grazed open, as round a place still lived in.
    float grazed = 0.0f;
    // Under trees.
    float forest = 0.0f;
    // The fell's lichen heath and dwarf birch.
    float fell = 0.0f;
    // Mire.
    float bog = 0.0f;
  };

  // What the animals read of the world.
  struct AnimalGround {
    // The ground's height at a point.
    std::function<float (float x, float z)> ground;
    // Whether a point is dry land (no standing or running water on it).
    std::function<bool (float x, float z)> dry;
    std::function<Pasture (float x, float z)> pasture;
    // Pushes a body of `radius` at `at` out of what stands there (tree
    // trunks, walls); may be empty.
    std::function<Vec3 (const Vec3& at, float radius)> clear;
    float period_x = 0.0f;
    float period_z = 0.0f;
  };

  // A place whose stock grazes round it: a farm still lived in, the
  // landing, a sanctuary still kept, and the trailhead's paddock.
  struct Fold {
    enum class Kind { farm, landing, sanctuary, trailhead };
    Kind kind = Kind::farm;
    Vec3 at {};
  };

  struct Animal {
    Species species = Species::cow;
    std::uint32_t seed = 0;
    std::uint32_t herd = 0;
    // Where its feet stand, under the middle of its body, and the way it
    // faces, as radians about the vertical from +z toward +x.
    Vec3 position {};
    float heading = 0.0f;
    // What it is doing, for how much longer, and where it is going.
    enum class Doing { grazing, standing, walking, alert, fleeing };
    Doing doing = Doing::standing;
    float left = 0.0f;
    Vec3 goal {};
    float speed = 0.0f;
    // What made it look up, and where that was.
    Vec3 threat {};
    // How far its gait has gone, in strides; how far its head is down to
    // the grass, 0 up to 1 cropping; and how far it is turned to look
    // aside, radians.
    float gait = 0.0f;
    float head = 0.0f;
    float look = 0.0f;
    float look_goal = 0.0f;
    // How much it has run lately, 0..1, which keeps it alert.
    float fright = 0.0f;
  };

  struct Herd {
    Species species = Species::cow;
    // Where it keeps to, and how far it strays from there.
    Vec3 home {};
    float range = 100.0f;
  };

  // Something the animals mind: someone on foot, or a machine.
  struct Presence {
    Vec3 position {};
    Vec3 velocity {};
    // A machine's engine is heard from further off.
    bool machine = false;
  };

  struct AnimalPlan {
    std::vector<Herd> herds;
    std::vector<Animal> animals;
  };

  // The stock round each fold and the wild herds across the world, more
  // of them near `trailhead` so that the first walk meets some.
  // Deterministic for a world.
  [[nodiscard]] AnimalPlan plan_animals (const AnimalGround& ground,
                                         std::span<const Fold> folds,
                                         const Vec3& trailhead,
                                         float world_x,
                                         float world_z,
                                         std::uint32_t seed);

  // The animals going about their day.
  class Animals {
  public:
    void rebuild (AnimalPlan plan);

    // Advances everything within `reach` of `focus` by `dt` seconds;
    // those further off wait where they are.
    void step (const AnimalGround& ground,
               std::span<const Presence> presences,
               const Vec3& focus,
               float reach,
               float dt);

    std::span<const Animal> animals () const noexcept {
      return m_plan.animals;
    }
    std::span<const Herd> herds () const noexcept {
      return m_plan.herds;
    }

  private:
    void choose (Animal& animal,
                 const Herd& herd,
                 const AnimalGround& ground,
                 const Vec3& mates);
    void startle (Animal& animal, const Vec3& from, const AnimalGround& ground);
    float rand (std::uint32_t& state) const;

    AnimalPlan m_plan;
    std::vector<std::uint32_t> m_random;
  };
}

#endif
