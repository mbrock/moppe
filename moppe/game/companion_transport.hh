#ifndef MOPPE_GAME_COMPANION_TRANSPORT_HH
#define MOPPE_GAME_COMPANION_TRANSPORT_HH

// How snapshots cross between machines (companion_link.hh): UDP datagrams
// on a local network or to a named host, a WebSocket room relay (`ws://`
// or `wss://`) for internet and browser play, or an in-memory hub for
// deterministic tests. Nothing here knows what is inside a packet.

#include <moppe/game/companion.hh>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace moppe::game {
  // Where a datagram came from or goes to: "ip:port" for UDP, "relay" for
  // a room relay, or a peer name on a memory hub.
  struct PeerEndpoint {
    std::string key;

    bool operator== (const PeerEndpoint&) const = default;
    bool operator< (const PeerEndpoint& other) const {
      return key < other.key;
    }
  };

  struct ReceivedDatagram {
    PeerEndpoint from;
    std::vector<std::uint8_t> bytes;
  };

  // Parsed `ws://host[:port]/path` or `wss://host[:port]/path`.
  struct RelayUrl {
    bool secure = false;
    std::string host;
    std::uint16_t port = 0;
    std::string path;
    std::string full;
  };

  // True when `target` names a WebSocket relay URL (`ws://` or `wss://`).
  bool is_relay_url (std::string_view target);

  // Expands shorthand (`room:pine-42` via `MOPPE_RELAY_URL`) into a relay
  // URL; returns `target` unchanged otherwise.
  std::string expand_multiplayer_target (std::string_view target);

  // Parses `ws://` or `wss://` into its parts, returning nullopt with
  // `error` set when malformed.
  std::optional<RelayUrl> parse_relay_url (std::string_view url,
                                           std::string& error);

  // Parses or formats a canonical IPv4 `a.b.c.d:port` endpoint key for
  // peer-to-peer mesh introductions.
  bool parse_ipv4_endpoint (const PeerEndpoint& endpoint,
                            std::uint32_t& ip_host_order,
                            std::uint16_t& port);
  PeerEndpoint format_ipv4_endpoint (std::uint32_t ip_host_order,
                                     std::uint16_t port);

  // One message-oriented, non-blocking transport channel.
  class PeerTransport {
  public:
    virtual ~PeerTransport () = default;

    // Sends one datagram to `to`.
    virtual void send_to (const PeerEndpoint& to,
                          std::span<const std::uint8_t> bytes) = 0;

    // Calls out for peers: LAN broadcast (or named host) on UDP, or room
    // fanout on a WebSocket relay.
    virtual void broadcast (std::span<const std::uint8_t> bytes) = 0;

    // Drains up to `limit` arrived datagrams without blocking.
    virtual void receive (std::vector<ReceivedDatagram>& out,
                          std::size_t limit = 64) = 0;

    // Drops any unread datagrams waiting in OS or client buffers.
    virtual void flush () = 0;

    // True when a single `broadcast()` reaches every peer in the session
    // (such as a room relay), so the link avoids sending duplicate copies.
    virtual bool shared_channel () const {
      return false;
    }

    // True once the channel can carry datagrams (e.g. after a WebSocket
    // handshake completes).
    virtual bool ready () const {
      return true;
    }

    // Human-readable summary for the log and in-game menu.
    virtual std::string description () const = 0;

    // Local UDP port when bound to one, else 0.
    virtual std::uint16_t local_port () const {
      return 0;
    }

    // Direct peer endpoint when configured with an explicit host[:port].
    virtual std::optional<PeerEndpoint> named_endpoint () const {
      return std::nullopt;
    }

    // Non-empty if the transport hit an unrecoverable error.
    virtual std::string last_error () const {
      return {};
    }
  };

  // Opens a UDP socket bound to `port` (or any free port if taken), calling
  // `address` (`host` or `host:port`) when non-empty or the LAN otherwise.
  std::unique_ptr<PeerTransport> open_udp_transport (const std::string& address,
                                                     std::uint16_t port,
                                                     std::string& error);

  // Opens a WebSocket room relay client (`ws://` or `wss://`).
  std::unique_ptr<PeerTransport> open_relay_transport (const std::string& url,
                                                       std::string& error);

  // Native Apple `NSURLSessionWebSocketTask` transport for `wss://`.
  std::unique_ptr<PeerTransport>
  open_apple_wss_transport (const std::string& url, std::string& error);

  // Deterministic in-memory packet hub for unit tests: simulates latency,
  // jitter, packet loss, and reordering across N virtual peers.
  class MemoryHub {
  public:
    struct Profile {
      duration_t latency = 0.0 * mp_units::si::second;
      duration_t jitter = 0.0 * mp_units::si::second;
      proportion_t loss = 0.0f * proportion[one];
      std::uint32_t seed = 1;
      bool shared_channel = false;
    };

    MemoryHub () : MemoryHub (Profile {}) {}
    explicit MemoryHub (Profile profile);

    link_time_t now () const {
      return m_now;
    }

    void advance (duration_t dt) {
      m_now += dt;
    }

    void set_now (link_time_t t) {
      m_now = t;
    }

    void set_profile (Profile profile);

    // Creates a new peer endpoint attached to this hub.
    std::unique_ptr<PeerTransport> join (std::string name);

    // Internal hooks used by hub-attached transports.
    void enqueue (const std::string& from,
                  const std::string& to,
                  std::span<const std::uint8_t> bytes);
    void enqueue_broadcast (const std::string& from,
                            std::span<const std::uint8_t> bytes);
    void drain (const std::string& who,
                std::vector<ReceivedDatagram>& out,
                std::size_t limit);
    void drop_for (const std::string& who);
    void leave (const std::string& who);
    bool shared_channel () const {
      return m_profile.shared_channel;
    }

  private:
    struct InFlight {
      std::string from;
      std::string to;
      link_time_t deliver_at = link_time (0.0);
      std::uint64_t order = 0;
      std::vector<std::uint8_t> bytes;
    };

    void schedule (const std::string& from,
                   const std::string& to,
                   std::span<const std::uint8_t> bytes);

    Profile m_profile;
    std::mt19937 m_rng;
    link_time_t m_now = link_time (0.0);
    std::uint64_t m_next_order = 0;
    std::vector<std::string> m_members;
    std::deque<InFlight> m_in_flight;
  };
}

#endif
