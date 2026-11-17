// The other player, as this game keeps them (companion.hh): the wire to
// them, what they have lately sent and how they look this frame, their
// machines as ours meet them, their picks, and the clocks the two players'
// snapshots are told apart by. MoppeGame's game_company.cc works it.
#ifndef MOPPE_GAME_COMPANIONSHIP_HH
#define MOPPE_GAME_COMPANIONSHIP_HH

#include <moppe/game/companion_link.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/game/outfit.hh>
#include <moppe/game/rescue.hh>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace moppe::game {
  struct Companionship {
    // Forgets the other player, who has gone or plays another world.
    void forget () {
      track.clear ();
      companion.reset ();
      picks.clear ();
      hours = -1.0;
      met = false;
      elsewhere = false;
    }

    // Tells this player what became of the other, on the HUD and the log.
    void notice (const char* words);

    // What the other player wears: whatever this one does not.
    Outfit their_outfit () const {
      return outfit == Outfit::blue_jacket ? Outfit::green_jacket
                                           : Outfit::blue_jacket;
    }

    // The passenger's seat of the other player's jeep, in the world.
    static Vec3 jeep_seat (const CompanionSnapshot& theirs);

    // The fore seat of the other player's boat, in the world.
    static Vec3 boat_seat (const CompanionSnapshot& theirs);

    // The wire to them, what they have lately sent, and how they look
    // this frame.
    std::unique_ptr<CompanionLink> link;
    CompanionTrack track;
    std::vector<CompanionSnapshot> arrived;
    std::optional<CompanionSnapshot> companion;
    // Their machines as ours bump into them, and where each stood a frame
    // ago, to tell how they move.
    std::vector<MachineBody> strangers;
    std::optional<std::array<Vec3, machine_count>> strangers_were;
    // Their picks in the order they made them, as far as heard, and the
    // same as mushrooms for the basket they carry.
    std::vector<std::uint32_t> picks;
    std::vector<MushroomSite> basket;
    // Their hour of the day; negative with no one to keep time with.
    double hours = -1.0;
    bool met = false;
    bool rode_along = false;
    // What this player wears; the other wears the other.
    Outfit outfit = Outfit::blue_jacket;
    bool elsewhere = false;
    std::uint32_t snapshots_sent = 0;
    // The clock this player's snapshots are stamped by, the link's clock
    // when it was last read, and how far the simulation has stepped since.
    double pose_clock = 0.0;
    double pose_heard = -1.0;
    double stepped = 0.0;
    // The last thing told of the other player, and when.
    const char* last_notice = nullptr;
    double notice_at = 0.0;
  };
}

#endif
