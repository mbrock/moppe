#ifndef MOPPE_GAME_SURROUNDINGS_HH
#define MOPPE_GAME_SURROUNDINGS_HH

// What a step of play reaches for beside the session and the ground: the
// world's planned mushrooms, places, and nest boxes, what the animals
// read of it, how the day passes, and the other player as last heard.
// With these, advance_game_session also tends the clock, the camp, the
// line, the pan, the basket, the beacons, and the animals and hens, so
// that a GameState checkpoint holds the whole of a step.

#include <moppe/game/animals.hh>
#include <moppe/game/fishing_state.hh>
#include <moppe/game/game_clock.hh>
#include <moppe/game/hens.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/game/places.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/trunk_field.hh>

#include <cstdint>
#include <optional>
#include <span>

namespace moppe::game {
  class BusLine;
  class GameSession;
  struct CompanionSnapshot;
  struct GameLogicState;

  struct Surroundings {
    const WorldParams* world = nullptr;
    const map::SurfaceGeometry* surface = nullptr;
    const terrain::WaterSheets* water = nullptr;
    const mov::TrunkField* trunks = nullptr;
    // Following the basket (MushroomPatch::follow) before the step.
    const MushroomPatch* mushrooms = nullptr;
    const PlacesPlan* places = nullptr;
    // The bus round the loop, to board and ride (bus.hh).
    const BusLine* bus = nullptr;
    std::span<const Nest> nests;
    std::uint32_t egg_seed = 0;
    const AnimalGround* animal_ground = nullptr;
    // The hours each second of play, and whether the clock turns at all.
    double day_pace = 0.0;
    bool day_turning = false;
    // How much light there is to see smoke and steam by, 0..1.
    float daylight = 1.0f;
    // The other player as drawn now, and their clock when it is known.
    const CompanionSnapshot* companion = nullptr;
    std::optional<clock_time_t> companion_clock;
  };

  // Lying in the hammock the hours run forty times as fast: one goes by
  // every three seconds of an ordinary day.
  inline constexpr float resting_haste = 40.0f;

  // Where the angler fishes from and the way they would cast: on foot,
  // the walker's feet and heading; at the boat's helm, the seat and the
  // way the view looks.
  struct Angler {
    Vec3 at;
    Vec3 ahead;
  };

  struct FishingWater {
    Vec3 aim;
    Waters waters = Waters::still;
    bool upland = false;
  };

  // The ground's height under `at`, without the roads.
  float ground_height_at (const map::SurfaceGeometry& surface, const Vec3& at);

  // Within a step of the hung hammock, beside its length.
  bool beside_hammock (const GameSession& session);
  // A fire is laid a step ahead of the walker, on dry and fairly level
  // ground.
  std::optional<Vec3> hearth_ahead (const GameSession& session,
                                    const Surroundings& around);
  // The fire a walker could cook on: the nearest one burning well within
  // a couple of steps, their own, the other player's, or a beacon.
  std::optional<Vec3> fire_at_hand (const GameSession& session,
                                    const Surroundings& around);
  bool beside_pan (const GameSession& session);
  // What there is to do about supper where the walker stands: fry what
  // the basket and the creel hold on a fire at hand, wait while it
  // fries, or eat it, done or burnt.
  enum class Supper : std::uint8_t { none, fry, frying, eat, eat_burnt };
  Supper supper_at_hand (const GameSession& session,
                         const Surroundings& around);
  // The beacon a walker stands at, if any.
  std::optional<std::size_t> beacon_at_hand (const GameSession& session,
                                             const Surroundings& around);
  // Whether the other player keeps beacon `i` burning.
  bool their_beacon (const GameLogicState& logic, std::size_t i);
  // The nest box with an egg in it the walker can reach into.
  std::optional<std::size_t> egg_at_hand (const GameSession& session,
                                          const Surroundings& around);
  std::optional<Angler> angler (const GameSession& session);
  // The river's current at `at`, level.
  Vec3 current_at (const Surroundings& around, const Vec3& at);
  // The boat only floats, its engine at rest and hardly moving through
  // the water (a river may carry it): a line can go out.
  bool boat_drifting (const GameSession& session, const Surroundings& around);
  // The open water the angler could cast into from where they are: as
  // far out ahead as a cast reaches where it is deep enough to float a
  // bait. From the helm, only over the bow or the sides, not back over
  // the long-tail.
  std::optional<FishingWater> water_ahead (const GameSession& session,
                                           const Surroundings& around);
  // The mushroom within the walker's reach.
  std::optional<std::uint32_t> mushroom_at_hand (const GameSession& session,
                                                 const Surroundings& around);
  // Where a fire would be laid: an old fire ring at hand, or level
  // ground a step ahead; and whether the walker stands by their own.
  std::optional<Vec3> fire_place (const GameSession& session,
                                  const Surroundings& around);
  bool beside_fire (const GameSession& session);

  // What the walker's hands do, when game/affordances.hh says a button
  // does it: pick a mushroom (a poisonous one is only looked at), take
  // an egg from a nest box, fry or eat supper, cast a line or strike and
  // reel it in, hang the hammock or take it down, lie in it or get up
  // (not in the first moment), light or put out a beacon, and light or
  // put out the fire.
  void pick (GameSession& session,
             const Surroundings& around,
             std::uint32_t mushroom);
  void take_egg (GameSession& session, std::size_t nest);
  void cook (GameSession& session, const Surroundings& around);
  void cast (GameSession& session, const Surroundings& around);
  void tend_line_by_hand (GameSession& session);
  void hang_hammock (GameSession& session);
  void take_down_hammock (GameSession& session);
  void lie_down (GameSession& session);
  void get_up (GameSession& session);
  void tend_beacon (GameSession& session,
                    const Surroundings& around,
                    std::size_t beacon);
  void light_fire (GameSession& session, const Vec3& hearth);
  void douse_fire (GameSession& session);

  // The steps advance_game_session takes about the walker before the
  // bodies move, once the buttons have done what they do
  // (game/affordances.hh): the clock, the line, the pan, and the camp,
  // each taking from `input` what it uses.
  void tend_by_hand (GameSession& session,
                     const Surroundings& around,
                     InputFrame& input,
                     seconds_t dt);
  // And after: the animals and the hens mind everyone about, and the
  // hens lay.
  void
  tend_animals (GameSession& session, const Surroundings& around, seconds_t dt);
}

#endif
