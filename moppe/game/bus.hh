#ifndef MOPPE_GAME_BUS_HH
#define MOPPE_GAME_BUS_HH

// The bus: round and round the loop (terrain::Road::loop), the road once
// round the world through the landing, stopping at stops of its own along
// it. Where a bus is follows from play's clock alone, by a timetable
// worked out from the line's length and its stops (accelerating away from
// each, keeping its pace, and braking for the next, and standing a while
// at each), so a checkpoint keeps it with no state of its own and each
// stop can tell how long till the next one comes. Several buses keep to
// the one timetable, evenly apart, so that one comes by every few
// minutes. They drive on the right.

#include <moppe/game/game_clock.hh>
#include <moppe/game/rescue.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/terrain/roads.hh>
#include <moppe/terrain/torus.hh>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace moppe::game {
  class GameSession;
  struct GameLogicState;
  struct PlaceGround;
  struct PlacesPlan;

  struct BusStop {
    std::string name;
    // How far along the loop from its start at the landing, the way the
    // buses go.
    meters_t along {};
    // The foot of the sign's post, on the verge to the right going along,
    // and the way the buses pass it, level. Anchored on the periodic torus.
    terrain::torus_position_t sign {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    // A shelter behind the sign, its open front to the road, where the
    // verge is level enough to stand one: its middle on the ground.
    std::optional<terrain::torus_position_t> shelter;
  };

  // Where a bus is, and how it stands: its position on the road under
  // its middle, unwrapped as the loop is (to be read modulo the world's
  // periods), the way it faces, and its up.
  struct BusPose {
    meters_t along {};
    position_t position {};
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    speed_t speed {};
    // Standing at a stop (into BusLine::stops), and the stop it stands
    // at or comes to next.
    std::optional<std::size_t> at_stop;
    std::size_t next_stop = 0;
    // For its drawing: how far its doors stand open, 0 shut to 1, and
    // its front wheels' turn (positive to the right) and how far its
    // wheels have rolled.
    proportion_t door_open {};
    radians_t steer {};
    radians_t wheel_spin {};
  };

  // What a stop's board says: a bus stands there and leaves in `wait`,
  // or the next is due in `wait`.
  struct BusDue {
    bool here = false;
    duration_t wait {};
  };

  class BusLine {
  public:
    // How the buses go: their pace between stops, how hard they gather
    // and lose it, how long they stand at each stop (a moment, long
    // enough to step on or off) and at the first, the landing, where
    // the round begins, and how often one comes by, about.
    static constexpr speed_t cruise = 12.5f * u::m / u::s;
    static constexpr metres_per_second_squared_t acceleration =
      0.8f * u::m / (u::s * u::s);
    static constexpr seconds_t dwell = 6.0f * u::s;
    static constexpr seconds_t layover = 20.0f * u::s;
    static constexpr seconds_t headway = 480.0f * u::s;
    // How far right of the road's line a bus keeps: its middle, near
    // enough the line that its 2.5 m body and its mirrors keep inside
    // the 3 m half carriageway round all but the tightest bends. The
    // buses all go one way round and never meet, so nothing comes the
    // other way.
    static constexpr meters_t lane = 0.9f * u::m;

    BusLine () = default;
    // The loop and the stops along it, in order from its start.
    BusLine (const terrain::Road& loop,
             std::vector<BusStop> stops,
             plan_extent_t period = {});

    bool empty () const noexcept {
      return m_along.size () < 2;
    }

    plan_extent_t period () const noexcept {
      return m_period;
    }

    meters_t length () const noexcept {
      return empty () ? 0.0f * u::m : m_along.back ();
    }

    std::span<const BusStop> stops () const noexcept {
      return m_stops;
    }

    // How many buses run, and how long one takes round.
    std::size_t buses () const noexcept {
      return m_buses;
    }

    seconds_t round_trip () const noexcept {
      return m_round;
    }

    // The road's line `along` the loop, any distance on or back: the
    // point on its centre (at the tread's height) and the way it goes
    // there, level. Unwrapped, so a lap on is a period on.
    position_t point (meters_t along) const;
    direction_t direction (meters_t along) const;

    // Each bus at `now`.
    std::vector<BusPose> where (play_time_t now) const;

    // The board at stop `stop` at `now`.
    BusDue due (std::size_t stop, play_time_t now) const;

  private:
    // The line: its points, unwrapped, the tread's height at each, and
    // how far along each is.
    std::vector<plan_position_t> m_points;
    std::vector<terrain::SurfaceElevation> m_height;
    std::vector<meters_t> m_along;
    // A lap on: the last point less the first.
    plan_displacement_t m_lap {};
    plan_extent_t m_period {};
    std::vector<BusStop> m_stops;
    // How far into a round a bus reaches each stop, and leaves it.
    std::vector<seconds_t> m_arrive, m_leave;
    seconds_t m_round {};
    std::size_t m_buses = 0;

    // How far into the round the bus `k` is.
    seconds_t phase (std::size_t k, play_time_t now) const;
  };

  // The stops along the world's loop, and the timetable: one at the
  // landing; one just past each road that branches off it, named for the
  // place it leads toward; one by each farm near it; and more between
  // them wherever they would lie far apart, with names of their own. Each
  // stands where the verge beside it is level and dry, off any bridge.
  // Empty when the world has no loop. `names` are the network's places'
  // (place_names.hh); `seed` rolls the stops' own.
  [[nodiscard]] BusLine plan_bus_line (const terrain::RoadNetwork& network,
                                       std::span<const std::string> names,
                                       const PlacesPlan& places,
                                       const PlaceGround& ground,
                                       std::uint32_t seed);

  // A bus as the machines' bumpers meet it (rescue.hh): a row of spheres
  // down its length and its weight, at the copy of the world nearest
  // `near`, never ours to push.
  [[nodiscard]] MachineBody
  bus_body (const BusPose& pose, const position_t& near, plan_extent_t period);

  // -- riding -------------------------------------------------------

  // The buses' clock: play's, run on by the lead that keeps it with the
  // other player's (GameLogicState::m_bus_lead), so that both see the
  // same buses and can ride one together.
  [[nodiscard]] play_time_t bus_time (const GameLogicState& logic);

  // Advance the timetable to five seconds before the next arrival.
  void
  wait_for_bus (GameLogicState& logic, const BusLine& line, std::size_t stop);

  // Where one gets on and off: on the road by its door, on its right,
  // and where a rider sits inside, in the world (unwrapped as the pose
  // is): `seat` 0 the front row's by the door (bus_model::seat_01), 1
  // the one beside it, for the other player.
  [[nodiscard]] position_t bus_door (const BusPose& pose);
  // The bus's model space (bus_model.hh) where it stands.
  [[nodiscard]] Mat4 bus_frame (const BusPose& pose);
  [[nodiscard]] position_t bus_seat (const BusPose& pose, std::size_t seat = 0);
  // That seat in the bus's model space, from its origin.
  [[nodiscard]] displacement_t bus_seat_in_model (std::size_t seat);

  // On foot within a few steps of the door of a bus standing at a stop:
  // which bus, to board.
  [[nodiscard]] std::optional<std::size_t> bus_at_hand (const BusLine& line,
                                                        const position_t& feet,
                                                        play_time_t now,
                                                        plan_extent_t period);

  // Gets on bus `bus` into seat `seat`, the walker left where they stood.
  void board_bus (GameSession& session,
                  const BusLine& line,
                  std::size_t bus,
                  std::size_t seat = 0);

  // The rider asks to get off: at once while the bus stands at a stop,
  // else at the next stop it comes to.
  void leave_bus (GameSession& session, const BusLine& line);

  // Each step on the bus: the seat goes where the bus is, and at a stop
  // the rider asked for, they get off, stepping down onto the verge by
  // the door.
  void ride_bus (GameSession& session, const BusLine& line);

  // The stop whose sign is nearest `at` within `reach`, across the seam.
  [[nodiscard]] std::optional<std::size_t> bus_stop_near (const BusLine& line,
                                                          const position_t& at,
                                                          meters_t reach,
                                                          plan_extent_t period);
}

#endif
