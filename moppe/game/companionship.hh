// The other player, as this game keeps them (companion.hh): the wire to
// them, what they have lately sent and how they look this frame, their
// machines as ours meet them, their picks, and the clocks the two players'
// snapshots are told apart by: finding them, the snapshots exchanged with
// them, riding along in their jeep or boat, and drawing them.
#ifndef MOPPE_GAME_COMPANIONSHIP_HH
#define MOPPE_GAME_COMPANIONSHIP_HH

#include <moppe/game/blob_shadow.hh>
#include <moppe/game/companion_link.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/game/outfit.hh>
#include <moppe/game/rescue.hh>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace moppe::game {
  class GameSession;
  struct GameLogicState;
  struct FrameView;
  struct HudState;
  struct InputFrame;
  struct WorldGround;

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

    // Opens the link to another player (companion_link.hh), calling
    // `address`, or the local network when it is empty. A launch that
    // cannot have one plays on alone.
    void seek (const std::string& address);

    // How this player looks now, for the other to draw.
    static CompanionSnapshot snapshot_of (const GameSession& session,
                                          std::uint32_t world,
                                          bool day_turning);

    // The companion's picks leave the ground here too, and fill the
    // basket they are seen carrying.
    void hear_of_picks (const CompanionSnapshot& theirs,
                        MushroomPatch& mushrooms);

    // Once a frame, first: hears what the other player has sent. Returns
    // them when they were met just now, for this player to make room
    // beside them (MoppeGame::make_room) before following them.
    std::optional<CompanionSnapshot> hear (MushroomPatch& mushrooms,
                                           std::uint32_t world);

    // Then: works out how they look just now, and tells them how this
    // one does.
    void follow (const GameSession& session,
                 std::uint32_t world,
                 bool day_turning,
                 seconds_t interval);

    // The other player's machines, for ours to bump into: where they
    // stand, and how they move from frame to frame (the jeep's motion is
    // told).
    void meet_strangers (const GameSession& session, seconds_t interval);

    // Keeping the seat beside the other player, or ahead of them in their
    // boat (companion.hh), as it moves, and leaving it: by the mount key,
    // or when they have gone. Their weight sits in our boat while they ride in
    // it.
    void ride_along (GameSession& session, InputFrame& input);

    // The other player as the world's second figure: their bike where
    // they left or ride it, themselves on foot, in the hammock, or under
    // the wing, and their camp.
    void draw (render::Renderer& r,
               render::DrawList& dl,
               float time,
               const GameLogicState& logic,
               const MushroomPatch& mushrooms,
               BlobShadow& blob,
               const map::SurfaceGeometry& surface,
               const WorldGround& ground);

    // The renderer has one lamp. With two fires burning it is the one
    // nearer the eye.
    void light_fire (FrameView& frame) const;

    void add_readings (HudState& state, const GameSession& session) const;

    // Tells this player what became of the other, on the HUD and the log.
    void notice (const char* words);

    // What the other player wears: whatever this one does not.
    Outfit their_outfit () const {
      return outfit == Outfit::blue_jacket ? Outfit::green_jacket
                                           : Outfit::blue_jacket;
    }

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
    // Scratch for their pan's contents as drawn.
    std::vector<MushroomSite> pan;
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
