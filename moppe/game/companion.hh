#ifndef MOPPE_GAME_COMPANION_HH
#define MOPPE_GAME_COMPANION_HH

// Multiple players in one world. Each machine simulates its own player and
// tells the others, some thirty times a second, how that player looks: the
// poses the renderer already draws from, the camp as it stands, the hour,
// and which mushrooms have been picked. Other machines draw each peer --
// their hiker, machines, camp, sling, rod, and rope -- a moment in the
// past, on a curve through the readings around it. Nothing here touches a
// socket (game/companion_link.hh does); these are the values and how they
// travel.

#include <moppe/game/camp_state.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/game_clock.hh>
#include <moppe/terrain/torus.hh>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace moppe::game {
  // Up to seven companions beside this player (eight players in all).
  inline constexpr std::size_t max_companions = 7;
  // Front eight seats of the bus (bus_model::seat_01..seat_08).
  inline constexpr std::size_t bus_passenger_seats = 8;
  // Bitmask covering every machine in FleetPoses.
  inline constexpr std::uint16_t all_machines_mask =
    static_cast<std::uint16_t> ((1u << machine_count) - 1u);

  // A link's clock (companion_link.hh): seconds since it opened, in
  // double so long sessions keep their steps. Each machine keeps its
  // own; a snapshot is stamped by its sender's, and the difference
  // between two machines' readings is only ever learnt by watching
  // (CompanionTrack).
  inline constexpr struct link_opened final
      : mp_units::absolute_point_origin<mp_units::isq::time> {
  } link_opened;

  using link_time_t =
    mp_units::quantity_point<mp_units::si::second, link_opened, double>;

  // The moment `seconds` after a link opened.
  constexpr link_time_t link_time (double seconds) {
    return link_opened + seconds * mp_units::si::second;
  }
  // The same as a plain number, for the wire.
  constexpr double seconds_on_link (link_time_t t) {
    return t.delta_from (link_opened).numerical_value_in (mp_units::si::second);
  }

  // What a world is made of, enough to explain why two games differ.
  struct WorldSummary {
    std::uint32_t tag = 0;
    std::uint32_t seed = 0;
    std::uint16_t resolution = 0;
    // Extent across one side in hectometres (100 m units; 100 = 10.0 km).
    std::uint16_t extent_hm = 0;
  };

  // How one player looks at one moment, as told to the others.
  struct CompanionSnapshot {
    // Which player, a number drawn at launch, and which of their
    // snapshots this is.
    std::uint64_t player = 0;
    std::uint32_t sequence = 0;
    // The sender's clock when the poses were true, and how long they have
    // played in this world (for deterministic spawn seniority).
    link_time_t sent = link_time (0.0);
    play_time_t play_time = moppe::game::play_time (0.0);
    // Which world they stand in (world_tag): poses mean nothing across
    // two different worlds.
    std::uint32_t world = 0;
    WorldSummary world_info {};
    // Their hour of the day, counted on from the first midnight; before
    // it (negative) when their day does not turn.
    clock_time_t day_hours = clock_time (0.0);

    // What they are doing; riding along, in which of `ride_target`'s
    // machines (MachineKind::passenger_seat).
    Doing mode = Doing::on_foot ();
    std::uint64_t ride_target = 0;
    // Which machines in `fleet` this packet carries; steady-state packets
    // pack active and round-robin machines to stay well below MTU, and the
    // link reassembles the full fleet on receipt.
    std::uint16_t fleet_mask = all_machines_mask;
    // Their machines, wherever they stand, and them on or in the one
    // they drive.
    FleetPoses fleet {};
    // Helicopter slung load state, reconstructed from their helicopter and
    // load poses when drawing.
    bool sling_hooked = false;
    Machine sling_load = Machine::caddy;
    bool sling_taut = false;
    // Fishing rod at the boat's helm, when casting from the boat.
    std::optional<RodPose> helm_rod;
    // Their buses' clock (bus.hh's bus_time), which the games keep
    // together; and on the bus, which bus and which seat (0..7).
    play_time_t bus_clock = moppe::game::play_time (0.0);
    std::uint8_t bus = 0;
    std::uint8_t bus_seat = 0;
    std::optional<GliderPose> glider;
    std::optional<WalkerPose> walker;

    // Their camp: the hammock and how much of the sleeper's weight is in
    // it, and the hearth and how high its fire burns.
    bool hung = false;
    HammockSite hammock {};
    proportion_t hammock_load = 0.0f * proportion[one];
    radians_t swing = 0.0f * si::radian;
    radians_t gaze_yaw = 0.0f * si::radian;
    inclination_t gaze_pitch = 1.1f * inclination[si::radian];
    bool hearth_laid = false;
    position_t hearth = position (Vec3 ());
    proportion_t fire_burn = 0.0f * proportion[one];
    // Which of the world's sanctuaries' beacons they keep burning, one
    // bit each, the first in the lowest (game/places.hh).
    std::uint32_t beacons = 0;

    // Their frying pan, if it is on a fire: where, its handle's way,
    // how done (camp.hh's pan_cooked), and what fries in it.
    bool pan_on = false;
    position_t pan_place = position (Vec3 ());
    heading_t pan_yaw = 0.0f * heading[si::radian];
    // From nothing through one, fried golden, to two, burnt black.
    proportion_t pan_cooked = 0.0f * proportion[one];
    std::uint8_t pan_count = 0;
    std::uint32_t pan_mushrooms[Pan::room] {};
    bool pan_has_fish = false;
    Fish pan_fish {};
    std::uint8_t pan_eggs = 0;

    // How many of their picks have left their basket for the pan.
    std::uint32_t pick_used = 0;
    // How many mushrooms they have picked in all, and some of them: the
    // plan's indices of picks `picks_from`, `picks_from + 1`, ...
    std::uint32_t pick_count = 0;
    std::uint32_t picks_from = 0;
    std::vector<std::uint32_t> picks;

    // Lying in the hammock rather than standing beside it.
    bool lying_down () const {
      return hung && mode == Doing::on_foot () &&
             hammock_load > 0.5f * proportion[one];
    }
  };

  // A snapshot carries at most this many picks.
  inline constexpr std::size_t companion_pick_window = 32;

  // The part of `picked` that snapshot `sequence` carries: the latest
  // picks in every other one, and between them a window that walks the
  // whole list, so a lost packet or a late arrival still hears of every
  // pick in the end. Returns the first pick's ordinal.
  std::uint32_t companion_pick_window_start (std::size_t picked,
                                             std::uint32_t sequence);

  // One number for the world a recipe makes, from its whole identity
  // (recipe_cache_identity): the seed, the size, the uplift, and the rest.
  std::uint32_t world_tag (std::string_view identity);

  // Explains why two worlds' tags do not match.
  std::string describe_world_mismatch (const WorldSummary& ours,
                                       const WorldSummary& theirs);

  // Copies every machine whose bit is set in `mask` from `incoming` into
  // `target`.
  void merge_fleet_poses (FleetPoses& target,
                          const FleetPoses& incoming,
                          std::uint16_t mask);

  // Shifts every world position in `snapshot` into the torus copy nearest
  // `near`, keeping coupled parts (trailer, hammock ends, rope, rod) intact.
  void rebase_snapshot (CompanionSnapshot& snapshot,
                        const position_t& near,
                        plan_extent_t period);

  // A snapshot as bytes on the wire, little-endian.
  std::vector<std::uint8_t> encode (const CompanionSnapshot& snapshot);

  // Nothing unless the bytes are a whole, sane snapshot of this version:
  // every number finite, every direction a direction.
  std::optional<CompanionSnapshot> decode (std::span<const std::uint8_t> bytes);

  // The look partway from `a` (at zero) to `b` (at one); `t` beyond one
  // carries the motion on. What does not blend -- the mode, who rides
  // what, the picks -- is `b`'s, and any body that leapt jumps on its own.
  CompanionSnapshot blend (const CompanionSnapshot& a,
                           const CompanionSnapshot& b,
                           blend_weight_t t);

  // The same, knowing the snapshots on either side (either may be
  // null): between `a` and `b` they then move along a curve that passes
  // each snapshot without a corner.
  CompanionSnapshot blend (const CompanionSnapshot* before,
                           const CompanionSnapshot& a,
                           const CompanionSnapshot& b,
                           const CompanionSnapshot* after,
                           blend_weight_t t);

  // One companion's recent snapshots, played back a little behind their
  // arrival so that there is nearly always one on either side of the
  // moment drawn. Snapshots arrive unevenly; the moment drawn moves on
  // evenly, with the frames, and is only steered by the arrivals.
  class CompanionTrack {
  public:
    // How long after their last word they are taken to have gone.
    static constexpr duration_t patience = 3.0 * mp_units::si::second;

    // Takes a snapshot that arrived at `now` on this machine's clock;
    // one older than the newest held is dropped.
    void push (CompanionSnapshot snapshot, link_time_t now);

    // Whether the companion has been heard from lately.
    bool present (link_time_t now) const;

    // How far in the past the companion is drawn: enough to cover the
    // longest wait between arrivals of late, so a steady link is
    // followed closely and an uneven one from farther back.
    duration_t delay () const;

    // Moves the moment drawn on by `elapsed`, the frame's length, and
    // tells how they look then; nothing once they have gone. Where word
    // has run out they are carried on a little way and slow to a stand,
    // and what later word corrects is eased rather than jumped.
    std::optional<CompanionSnapshot> advance (link_time_t now,
                                              duration_t elapsed);

    void clear ();

  private:
    static constexpr std::size_t slip_count = machine_count + 3;

    // How they looked at `at` on their clock, going by the first
    // `count` snapshots held.
    CompanionSnapshot look (std::size_t count, link_time_t at) const;

    std::deque<CompanionSnapshot> m_snapshots;
    // The sender's clock less this machine's, eased over arrivals: a
    // reading of theirs less this much is the same moment on this one.
    duration_t m_offset = 0.0 * mp_units::si::second;
    link_time_t m_heard = link_time (-1e9);
    // The longest wait between arrivals of late.
    duration_t m_gap = 0.05 * mp_units::si::second;
    // How many snapshots have come since the last advance.
    std::size_t m_fresh = 0;
    // The moment drawn, on the sender's clock, and how fast that clock
    // is found to run against this one.
    bool m_playing = false;
    link_time_t m_play = link_time (0.0);
    quantity<scale_factor[one], double> m_rate = 1.0 * scale_factor[one];
    // How far every machine, the tractor's trailer, the walker, and the
    // glider are still drawn from where the snapshots put them.
    std::array<displacement_t, slip_count> m_slip {};
  };

  class GameSession;
  struct GameLogicState;

  // Whose machine a passenger seat belongs to.
  struct SeatTarget {
    std::uint64_t player = 0;
    Machine machine = Machine::jeep;
  };

  // The passenger's seat of another player's `machine`, in the world,
  // for those that have one (MachineKind::passenger_seat).
  position_t passenger_seat (const CompanionSnapshot& theirs, Machine machine);

  // On foot beside an unoccupied passenger seat of any companion's
  // machine -- the one they drive, or one standing on land -- and nearer
  // it than anything of one's own to ride: which peer and machine, if any.
  std::optional<SeatTarget>
  seat_within_reach (const GameSession& session,
                     std::span<const CompanionSnapshot> companions);

  // Takes the passenger's seat of `theirs`'s `machine`, as the mount key
  // does there (game/affordances.hh).
  void take_seat (GameSession& session,
                  const CompanionSnapshot& theirs,
                  Machine machine);

  // The seat moves with the other player's machine.
  void keep_seat (GameLogicState& state, const CompanionSnapshot& theirs);

  // Keeps this game's buses' clock (bus_time) with the leading peer's:
  // a game behind by more than a moment jumps ahead, and otherwise every
  // player follows the peer with the highest player ID so no two chase
  // each other on.
  void keep_bus_time (GameLogicState& state,
                      const CompanionSnapshot& theirs,
                      duration_t behind,
                      std::uint64_t player);
}

#endif
