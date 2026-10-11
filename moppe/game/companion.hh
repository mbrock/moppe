#ifndef MOPPE_GAME_COMPANION_HH
#define MOPPE_GAME_COMPANION_HH

// Two players in one world. Each machine simulates its own player and
// tells the other, some thirty times a second, how that player looks: the
// poses the renderer already draws from, the camp as it stands, the hour,
// and which mushrooms have been picked. The other machine draws that as a
// second hiker, bike, jeep, or glider -- the companion -- a moment in the past,
// on a curve through the readings around it. Nothing here touches a
// socket (game/companion_link.hh does); these are the values and how they
// travel.

#include <moppe/game/camp_state.hh>
#include <moppe/game/frame_view.hh>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace moppe::game {
  // How one player looks at one moment, as told to the other.
  struct CompanionSnapshot {
    // Which player, a number drawn at launch, and which of their
    // snapshots this is.
    std::uint64_t player = 0;
    std::uint32_t sequence = 0;
    // The sender's clock when the poses were true, seconds.
    double sent = 0.0;
    // Which world they stand in (world_tag): poses mean nothing across
    // two different worlds.
    std::uint32_t world = 0;
    // Their hour of the day, counted on from the first midnight.
    double day_hours = 0.0;

    Mode mode = M_FOOT;
    VehiclePose bike {};
    // Their jeep, wherever it stands, and how it is moving: someone
    // riding along in it moves with it.
    JeepPose jeep {};
    Vec3 jeep_velocity {};
    std::optional<GliderPose> glider;
    std::optional<WalkerPose> walker;

    // Their camp: the hammock and how much of the sleeper's weight is in
    // it, and the hearth and how high its fire burns.
    bool hung = false;
    HammockSite hammock {};
    float hammock_load = 0.0f;
    float swing = 0.0f;
    float gaze_yaw = 0.0f;
    float gaze_pitch = 1.1f;
    bool hearth_laid = false;
    Vec3 hearth {};
    float fire_burn = 0.0f;

    // Their frying pan, if it is on a fire: where, its handle's way,
    // how done (camp.hh's pan_cooked), and the mushrooms in it.
    bool pan_on = false;
    Vec3 pan_place {};
    float pan_yaw = 0.0f;
    float pan_cooked = 0.0f;
    std::uint8_t pan_count = 0;
    std::uint32_t pan_mushrooms[Pan::room] {};

    // How many of their picks have left their basket for the pan.
    std::uint32_t pick_used = 0;
    // How many mushrooms they have picked in all, and some of them: the
    // plan's indices of picks `picks_from`, `picks_from + 1`, ...
    std::uint32_t pick_count = 0;
    std::uint32_t picks_from = 0;
    std::vector<std::uint32_t> picks;

    // Lying in the hammock rather than standing beside it.
    bool lying_down () const {
      return hung && mode == M_FOOT && hammock_load > 0.5f;
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

  // One number for the world a recipe makes, as far as two players need
  // to agree on it.
  std::uint32_t world_tag (std::uint32_t seed, int resolution, int profile);

  // A snapshot as bytes on the wire, little-endian.
  std::vector<std::uint8_t> encode (const CompanionSnapshot& snapshot);

  // Nothing unless the bytes are a whole, sane snapshot of this version:
  // every number finite, every direction a direction.
  std::optional<CompanionSnapshot> decode (std::span<const std::uint8_t> bytes);

  // The look partway from `a` (at zero) to `b` (at one); `t` beyond one
  // carries the motion on. What does not blend -- the mode, who rides
  // what, the picks -- is `b`'s, and so is the whole of it across a leap
  // too long to have been travelled.
  CompanionSnapshot
  blend (const CompanionSnapshot& a, const CompanionSnapshot& b, float t);

  // The same, knowing the snapshots on either side (either may be
  // null): between `a` and `b` they then move along a curve that passes
  // each snapshot without a corner.
  CompanionSnapshot blend (const CompanionSnapshot* before,
                           const CompanionSnapshot& a,
                           const CompanionSnapshot& b,
                           const CompanionSnapshot* after,
                           float t);

  // The companion's recent snapshots, played back a little behind their
  // arrival so that there is nearly always one on either side of the
  // moment drawn. Snapshots arrive unevenly; the moment drawn moves on
  // evenly, with the frames, and is only steered by the arrivals.
  class CompanionTrack {
  public:
    // How long after their last word they are taken to have gone,
    // seconds.
    static constexpr double patience = 3.0;

    // Takes a snapshot that arrived at `now` on this machine's clock;
    // one older than the newest held is dropped.
    void push (CompanionSnapshot snapshot, double now);

    // Whether the companion has been heard from lately.
    bool present (double now) const;

    // How far in the past the companion is drawn, seconds: enough to
    // cover the longest wait between arrivals of late, so a steady link
    // is followed closely and an uneven one from farther back.
    double delay () const;

    // Moves the moment drawn on by `elapsed`, the frame's length, and
    // tells how they look then; nothing once they have gone. Where word
    // has run out they are carried on a little way and slow to a stand,
    // and what later word corrects is eased rather than jumped.
    std::optional<CompanionSnapshot> advance (double now, double elapsed);

    void clear ();

  private:
    // How they looked at `at` on their clock, going by the first
    // `count` snapshots held.
    CompanionSnapshot look (std::size_t count, double at) const;

    std::deque<CompanionSnapshot> m_snapshots;
    // The sender's clock less this machine's, eased over arrivals.
    double m_offset = 0.0;
    double m_heard = -1e9;
    // The longest wait between arrivals of late.
    double m_gap = 0.05;
    // How many snapshots have come since the last advance.
    std::size_t m_fresh = 0;
    // The moment drawn, on the sender's clock, and how fast that clock
    // is found to run against this one.
    bool m_playing = false;
    double m_play = 0.0;
    double m_rate = 1.0;
    // How far the bike, jeep, walker, and glider are still drawn from
    // where the snapshots put them.
    std::array<Vec3, 4> m_slip {};
  };
}

#endif
