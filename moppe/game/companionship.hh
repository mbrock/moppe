// The other players, as this game keeps them (companion.hh): the wire to
// them, each peer's track and rebased snapshot this frame, their machines
// as ours meet them, their picks, their snow tracks, riding along in any
// peer's vehicle, and drawing every peer cleanly across torus seams.
#ifndef MOPPE_GAME_COMPANIONSHIP_HH
#define MOPPE_GAME_COMPANIONSHIP_HH

#include <moppe/game/blob_shadow.hh>
#include <moppe/game/boat_render.hh>
#include <moppe/game/companion_link.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/game/outfit.hh>
#include <moppe/game/rescue.hh>
#include <moppe/game/snow.hh>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace moppe::game {
  class GameSession;
  struct GameLogicState;
  struct FrameView;
  struct HudState;
  struct InputFrame;
  struct WorldGround;

  struct RemotePeer {
    std::uint64_t player = 0;
    std::uint32_t slot = 1;
    std::string address;
    Outfit outfit = Outfit::green_jacket;
    CompanionTrack track;
    std::optional<CompanionSnapshot> snapshot;
    std::optional<std::array<position_t, machine_count>> strangers_were;
    std::vector<std::uint32_t> picks;
    SnowTrail snow {};
    clock_time_t hours = clock_time (-1.0);
    play_time_t last_play_time = play_time (0.0);
    bool announced = false;
  };

  struct Companionship {
    // Forgets every remote peer (e.g. when switching worlds or disconnecting).
    void forget () {
      peers.clear ();
      active.clear ();
      active_outfits.clear ();
      strangers.clear ();
      hours = clock_time (-1.0);
      rode_along = false;
      elsewhere = false;
      elsewhere_note.clear ();
      outfit = Outfit::blue_jacket;
    }

    // Opens the link to other players (companion_link.hh), calling
    // `address` (direct `host[:port]`, `ws[s]://.../room`, `room:<name>`),
    // or the local network when empty. Returns false with `error` set on
    // failure (a launch that cannot open one still plays on alone).
    bool seek (const std::string& address, std::string* error_out = nullptr);

    // Leaves the current multiplayer session and forgets remote peers.
    void disconnect ();

    // How this player looks now, for the others to draw.
    static CompanionSnapshot snapshot_of (const GameSession& session,
                                          const WorldSummary& world,
                                          bool day_turning);

    // A peer's picks leave the ground here too, and fill the basket they
    // carry.
    static void hear_of_picks (RemotePeer& peer,
                               const CompanionSnapshot& theirs,
                               MushroomPatch& mushrooms);

    // Once a frame, first: hears what other players have sent. Returns
    // any newly met snapshots for this player to make room beside them
    // (MoppeGame::make_room) before following them.
    std::vector<CompanionSnapshot> hear (MushroomPatch& mushrooms,
                                         const WorldSummary& world);

    // Then: advances every peer's track, rebases poses across torus seams
    // near our subject, updates collision bodies and snow tracks, and
    // sends our snapshot when due.
    void follow (GameSession& session,
                 const WorldSummary& world,
                 const WorldGround& ground,
                 bool day_turning,
                 seconds_t interval);

    // Every peer's machines, for ours to bump into: where they stand,
    // and how they move from frame to frame.
    void meet_strangers (const GameSession& session, seconds_t interval);

    // Keeping the passenger's seat in whichever peer's machine we sit in,
    // and leaving it by the mount key or when they go.
    void ride_along (GameSession& session, InputFrame& input);

    // Keeps this game's buses with the leading peer's timetable.
    void keep_time (GameSession& session);

    // Draws every peer: their fleet (with riders & boat rod), sling,
    // camp, pan (with fish & eggs), walker (with rope & rod), and glider.
    void draw (render::Renderer& r,
               render::DrawList& dl,
               seconds_t time,
               const GameLogicState& logic,
               const MushroomPatch& mushrooms,
               BlobShadow& blob,
               const map::SurfaceGeometry& surface,
               const WorldGround& ground);

    // Shadow casters for every peer's fleet.
    void cast_shadows (render::Renderer& r, render::DrawList& caster_dl) const;

    // Additive glows for every peer's bike flames and campfires.
    void draw_glows (render::Renderer& r,
                     const FrameView& frame,
                     bool vehicle_effects) const;

    // Watercraft poses for wake rendering.
    std::vector<BoatWakes::PeerCraft> peer_crafts () const;

    // The renderer has one lamp. With multiple fires burning it is the
    // one nearer the eye.
    void light_fire (FrameView& frame) const;

    void add_readings (HudState& state, const GameSession& session) const;

    // Tells this player what became of companions, on the HUD and the log.
    void notice (std::string words);

    // Recomputes deterministic outfits for this player and all peers from
    // their sorted player IDs.
    void recompute_outfits ();

    // Outfit of peer with `player_id`, if present.
    std::optional<Outfit> outfit_of (std::uint64_t player_id) const;

    // Active rebased snapshots for surroundings/HUD/rendering this frame.
    std::span<const CompanionSnapshot> snapshots () const {
      return active;
    }

    // The wire, all tracked peers, and the active frame's rebased views.
    std::unique_ptr<CompanionLink> link;
    std::string target_address;
    std::string last_error;
    std::vector<RemotePeer> peers;
    std::vector<CompanionSnapshot> arrived;
    std::vector<CompanionSnapshot> active;
    std::vector<Outfit> active_outfits;
    // Their machines as ours bump into them.
    std::vector<MachineBody> strangers;
    // Scratch for a peer's basket and pan as drawn.
    std::vector<MushroomSite> basket;
    std::vector<MushroomSite> pan;
    // Leading day hour across peers; negative when none have a turning day.
    clock_time_t hours = clock_time (-1.0);
    bool rode_along = false;
    // What this player wears; peers wear distinct outfits by sorted ID.
    Outfit outfit = Outfit::blue_jacket;
    bool elsewhere = false;
    std::string elsewhere_note;
    std::uint32_t snapshots_sent = 0;
    // The clock this player's snapshots are stamped by.
    link_time_t pose_clock = link_time (0.0);
    link_time_t pose_heard = link_time (-1.0);
    duration_t stepped = 0.0 * si::second;
    // The last thing told of other players, and when.
    std::string last_notice;
    link_time_t notice_at = link_time (0.0);
  };
}

#endif
