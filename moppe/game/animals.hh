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
// The farmsteads' barns keep stock indoors too: horses and cows tied in
// their stalls with their heads to the manger, and Linderöd pigs loose in
// their pen; they eat, look round at whoever comes in, and stay put.

#include <moppe/gfx/math.hh>
#include <moppe/terrain/domain.hh>

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
    roe_deer,
    pig
  };
  constexpr int species_count = 7;

  // How far a gait has gone, in whole strides (cycles of the legs).
  using strides_t = quantity<isq::rotation[one], float>;
  // How a beast carries its head (moppe/quantities.hh).
  using head_carriage_t = quantity<head_carriage[one], float>;

  // What a species is like to meet.
  struct SpeciesTraits {
    const char* name;
    const char* plural;
    // Walking and running pace, m/s.
    speed_t walk = 1.0f * u::m / u::s;
    speed_t run = 3.0f * u::m / u::s;
    // How near someone on foot may come before it moves off, m; anything
    // moving fast is minded from further off.
    meters_t wary = 10.0f * u::m;
    // How far it goes once startled, m.
    meters_t flight = 30.0f * u::m;
    // Its length from chest to rump, m: its room among its herd.
    meters_t length = 1.6f * u::m;
    // How far one stride carries it at a walk, m.
    meters_t stride = 1.6f * u::m;
    // Kept stock (as opposed to wild).
    bool stock = false;
    // Runs by bounding (the roe deer) rather than trotting.
    bool bounds = false;
  };
  const SpeciesTraits& traits (Species species);

  // What the ground offers an animal at a point, each 0..1.
  struct Pasture {
    // Grazed open, as round a place still lived in.
    proportion_t grazed = 0.0f * proportion[one];
    // Under trees.
    proportion_t forest = 0.0f * proportion[one];
    // The fell's lichen heath and dwarf birch.
    proportion_t fell = 0.0f * proportion[one];
    // Mire.
    proportion_t bog = 0.0f * proportion[one];
    // Built carriageway and its immediate verge, 0 away from roads to 1 on
    // the road itself.
    proportion_t road = 0.0f * proportion[one];
  };

  // What the animals read of the world.
  struct AnimalGround {
    // The ground's height at a point.
    terrain::ElevationField ground;
    // Whether a point is dry land (no standing or running water on it).
    terrain::PlanField<bool> dry;
    terrain::PlanField<Pasture> pasture;
    // Pushes a body of `radius` at `at` out of what stands there (tree
    // trunks, walls); may be empty.
    std::function<position_t (const position_t& at, meters_t radius)> clear;
    // The world's period; zero in a lane that does not wrap.
    plan_extent_t period {};
  };

  // A place whose stock grazes round it: a farm still lived in, the
  // landing, a sanctuary still kept, and the trailhead's paddock.
  struct Fold {
    enum class Kind { farm, landing, sanctuary, trailhead };
    Kind kind = Kind::farm;
    position_t at {};
  };

  struct Animal {
    Species species = Species::cow;
    std::uint32_t seed = 0;
    std::uint32_t herd = 0;
    // Where its feet stand, under the middle of its body, and the way it
    // faces, as radians about the vertical from +z toward +x.
    position_t position {};
    heading_t heading {};
    // What it is doing, for how much longer, and where it is going.
    enum class Doing { grazing, standing, walking, alert, fleeing };
    Doing doing = Doing::standing;
    seconds_t left {};
    plan_position_t goal {};
    speed_t speed {};
    // What made it look up, and where that was.
    position_t threat {};
    // How far its gait has gone, in strides; how far its head is down to
    // the grass, 0 up to 1 cropping; and how far it is turned to look
    // aside, radians.
    strides_t gait {};
    head_carriage_t head {};
    radians_t look {};
    radians_t look_goal {};
    // How much it has run lately, 0..1, which keeps it alert.
    proportion_t fright {};
    // Kept in a barn (see Stall): where, and whether it is tied there or
    // goes about its pen.
    bool indoors = false;
    bool tied = false;
    position_t pen {};
    heading_t pen_heading {};
    meters_t pen_along {};
    meters_t pen_across {};
  };

  // Stock kept in a barn: `count` beasts tied at `at` facing `heading`
  // (radians about the vertical from +z toward +x) -- their muzzles at
  // the manger -- or, with half extents along and across that heading, as
  // many loose in a pen whose middle is `at`. On the barn's floor.
  struct Stall {
    Species species = Species::cow;
    position_t at {};
    heading_t heading {};
    meters_t half_along {};
    meters_t half_across {};
    int count = 1;
  };

  struct Herd {
    Species species = Species::cow;
    // Where it keeps to, and how far it strays from there.
    position_t home {};
    meters_t range = 100.0f * u::m;
  };

  // Something the animals mind: someone on foot, or a machine.
  struct Presence {
    position_t position {};
    velocity_t velocity {};
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
                                         const position_t& trailhead,
                                         const plan_extent_t& world,
                                         std::uint32_t seed,
                                         std::span<const Stall> stalls = {});

  // The animals going about their day.
  class Animals {
  public:
    void rebuild (AnimalPlan plan);

    // Advances everything within `reach` of `focus` by `dt` seconds;
    // those further off wait where they are.
    void step (const AnimalGround& ground,
               std::span<const Presence> presences,
               const position_t& focus,
               meters_t reach,
               seconds_t dt);

    std::span<const Animal> animals () const noexcept {
      return m_plan.animals;
    }
    std::span<const Herd> herds () const noexcept {
      return m_plan.herds;
    }

    // What moves as they go about their day (GameState): the herds are
    // the plan's and stay.
    struct State {
      std::vector<Animal> animals;
      std::vector<std::uint32_t> random;
    };
    State state () const {
      return { m_plan.animals, m_random };
    }
    void restore (const State& state) {
      m_plan.animals = state.animals;
      m_random = state.random;
    }

  private:
    void choose (Animal& animal,
                 const Herd& herd,
                 const AnimalGround& ground,
                 const position_t& mates);
    void startle (Animal& animal,
                  const position_t& from,
                  const AnimalGround& ground);
    void keep_indoors (Animal& animal,
                       const AnimalGround& ground,
                       const Presence* nearest,
                       meters_t nearest_distance,
                       seconds_t dt);
    float rand (std::uint32_t& state) const;

    AnimalPlan m_plan;
    std::vector<std::uint32_t> m_random;
  };
}

#endif
