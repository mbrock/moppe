// Where the machines stand when a world begins: the jeep by a road, the
// tractor and the bikes about the trailhead, the boat and the jet ski at
// the water's edge, the Caddy above the boat's shore, the helicopter on
// a pad of its own. It keeps where the boat was moored and, on a river,
// the course down to the sea that the water autopilot follows.
#ifndef MOPPE_GAME_PARKING_HH
#define MOPPE_GAME_PARKING_HH

#include <moppe/game/game_session.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/places.hh>
#include <moppe/game/world_ground.hh>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace moppe::game {
  // What parking reads and moves: the session's machines, the world
  // under them, the places, and where the player started.
  struct ParkingLot {
    GameSession& session;
    WorldGround ground;
    const PlacesPlan& places;
    const mov::TrunkField& trunks;
    position_t spawn;
  };

  class Parking {
  public:
    // How far aside the later of two players parks the machines that
    // stand at places in the world, so that theirs and ours do not stand
    // in one another.
    void set_aside (const displacement_t& aside) {
      m_aside = aside;
    }

    // The boat lies at the water's edge of the sea, the nearest open
    // shore of it to the spawn (or, with no sea in reach or with
    // `MOPPE_BOAT=lake`, of a lake big enough to take it out on): afloat
    // a few metres out with its bow drawn up toward the land, as left by
    // someone gone ashore. `outward` turns it to face the open water.
    // MOPPE_BOAT=river: the boat lies on the greatest river, as far up
    // from the sea as the water stays deep enough to drive it the whole
    // way down, facing downstream.
    void park_boat (const ParkingLot& lot, bool outward = false);

    // The jet ski lies afloat by the boat: at the landing, alongside the
    // jetty's other side; on an open shore or a river, a few metres
    // along from the boat, wherever the water there will float it. Its
    // bow is to the land as the boat's is, or with `outward` to the
    // open water.
    void park_jet_ski (const ParkingLot& lot, bool outward = false);

    // The jeep is pulled off onto the verge of the nearest cart road, a
    // few hundred metres out from the trailhead, nose along the road as
    // if left there for the walk in; beside the bike when no road comes
    // near enough.
    void park_jeep (const ParkingLot& lot) const;

    // The tractor stands out on the open ground ahead on the bike's
    // other side, turned to show its flank, as if left there at the end
    // of a day's work; the ground found is room for it and the trailer
    // behind it, some six and a half metres from nose to tailgate.
    void park_tractor (const ParkingLot& lot) const;

    // The Caddy is parked by the sea, on the nearest level, open ground
    // above the shore the boat was left at, nose to the water as if its
    // driver had stood looking out; with no boat, at the trailhead.
    void park_caddy (const ParkingLot& lot) const;
    void park_snowmobile (const ParkingLot& lot) const;

    // The mountainbike is left at the top of the trail's nearest climb,
    // the highest point of it within a kilometre or so, off the tread and
    // facing back down the way it came up, ready for the descent; a few
    // steps off the motocross when the trail does not climb.
    void park_mountain_bike (const ParkingLot& lot) const;

    // The Long John, a cargo bike for errands, stands outside the nearest
    // farmhouse still lived in, or else at the nearest hamlet the roads
    // join -- a lake's outlet or the landing -- pointed home toward the
    // trailhead; back down the trail behind the motocross when none is
    // near.
    void park_long_john (const ParkingLot& lot) const;

    // The helicopter stands on a pad of its own a few hundred metres
    // from the trailhead: the levellest open ground round there, raised
    // above its surroundings where it can be, as a knoll or a shoulder
    // makes a landing site, with no tree within reach of its rotor;
    // clear of `avoid` (the other player's) when given one.
    void park_helicopter (const ParkingLot& lot,
                          std::optional<position_t> avoid = std::nullopt) const;

    // What the water autopilot steers: how fast it is going, for how
    // far ahead it must mind the shallows; whether it follows the
    // river's course where there is one; how far it looks for open
    // water; and how much of that it cares to have ahead of it before
    // the weave matters more.
    struct WaterPilot {
      speed_t speed = 0.0f * u::m / u::s;
      bool follow_route = false;
      meters_t sight = 90.0f * u::m;
      meters_t room = 60.0f * u::m;
    };

    // Steers something afloat at `at`, heading `heading`, along the
    // river's course the boat was moored on if there is one to follow,
    // and otherwise over open water in a lazy weave, steering clear of
    // shallows it sees ahead.
    InputFrame water_autopilot (const WorldGround& ground,
                                seconds_t t,
                                const position_t& at,
                                direction_t heading,
                                const WaterPilot& craft);

  private:
    bool moor_on_river (const ParkingLot& lot);
    bool moor_boat (const ParkingLot& lot, bool sea_only, bool outward);

    // The trailhead's frame: the bike's heading on the level, and its
    // left.
    static std::pair<direction_t, direction_t>
    bike_frame (const ParkingLot& lot);

    // Whether a machine `room` round at `at` keeps clear of the
    // machines already parked there.
    static bool clear_of_machines (const ParkingLot& lot,
                                   const position_t& at,
                                   meters_t room);

    static void report_parked (const ParkingLot& lot,
                               const char* name,
                               const position_t& at,
                               const char* where);

    // The shore the boat was left at, when it was.
    std::optional<position_t> m_mooring;
    // The way out to open water from there, and where the boat lay and
    // how it faced.
    std::optional<direction_t> m_mooring_out;
    std::optional<std::pair<position_t, direction_t>> m_boat_berth;
    displacement_t m_aside = displacement (Vec3 ());
    // MOPPE_BOAT=river: the river's course down to the sea, which the
    // boat autopilot follows, and the next point of it ahead.
    std::vector<position_t> m_boat_route;
    std::size_t m_boat_route_at = 0;
  };

  // Where a point unwrapped across the world's seam stands in the lap
  // nearest `spawn`, on the ground.
  position_t near_spawn (const WorldGround& ground,
                         const position_t& spawn,
                         const position_t& at);

  // How far `at` stands from `spawn` on the level.
  meters_t from_spawn (const position_t& spawn, const position_t& at);

  // MOPPE_WALK=haul: the tractor is stuck where it stands and the walker
  // stands before it, facing away, to haul it out on a rope.
  // MOPPE_WALK=bump: the jeep stands back from the tractor's flank, aimed
  // at it, with the walker at its door.
  void stage_rescue (GameSession& session, const WorldGround& ground);
}

#endif
