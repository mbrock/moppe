#ifndef MOPPE_GAME_COMPANION_LINK_HH
#define MOPPE_GAME_COMPANION_LINK_HH

// The wire between players (game/companion.hh): sits over any
// PeerTransport (LAN/direct UDP, a WebSocket room relay, or an in-memory
// test hub), discovers and maintains up to `max_companions` peers in the
// same world, packs steady-state fleet deltas under MTU, introduces peers
// to each other into a direct mesh, and filters out-of-order arrivals.

#include <moppe/game/companion.hh>
#include <moppe/game/companion_transport.hh>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace moppe::game {
  class CompanionLink {
  public:
    static constexpr std::uint16_t default_port = 27960;
    using ClockFn = std::function<link_time_t ()>;

    struct Config {
      // Direct `host[:port]`, WebSocket relay `ws[s]://host[:port]/room`,
      // `room:<name>` shorthand, or empty to discover on the local network.
      std::string address;
      // Local UDP port to listen on (ignored for relay URLs). Taken already,
      // any free port serves.
      std::uint16_t port = default_port;
    };

    // Opens a live UDP or WebSocket relay link. Returns null with `error`
    // set if the transport cannot be opened.
    static std::unique_ptr<CompanionLink> open (const Config& config,
                                                std::string& error);

    // Wraps a caller-supplied transport (such as MemoryHub::join for tests)
    // with an optional deterministic clock and player ID.
    static std::unique_ptr<CompanionLink>
    attach (std::unique_ptr<PeerTransport> transport,
            ClockFn clock = {},
            std::uint64_t player_override = 0);

    ~CompanionLink ();
    CompanionLink (const CompanionLink&) = delete;
    CompanionLink& operator= (const CompanionLink&) = delete;

    // This player's number, and the link's own clock.
    std::uint64_t player () const;
    link_time_t now () const;

    // Whether it is time to send: thirty times a second while speaking to
    // peers, twice a second while calling out for them.
    bool due () const;

    // Stamps the snapshot with this player, the next sequence number,
    // world metadata, and `taken`, packs its fleet mask, and sends it.
    void send (CompanionSnapshot snapshot, link_time_t taken);

    // Every complete snapshot that has arrived from any peer in this world
    // since the last call, in arrival order, each newer than the last taken
    // from that peer.
    void receive (std::vector<CompanionSnapshot>& arrived);

    // The world this game stands in. Changing worlds clears peers and
    // flushes buffered packets from the previous world.
    void set_world (const WorldSummary& world);
    void set_world (std::uint32_t world);

    // Whether someone in another world was heard since the last call, and
    // their world summary if available.
    bool heard_elsewhere ();
    std::optional<WorldSummary> elsewhere_world ();

    // Diagnostics for the log and in-game multiplayer menu.
    std::size_t peer_count () const;
    std::string address_of (std::uint64_t player_id) const;
    std::string companion_address () const;
    std::uint16_t port () const;
    bool ready () const;
    std::string transport_description () const;
    std::string last_error () const;

  private:
    struct State;
    explicit CompanionLink (std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
  };
}

#endif
