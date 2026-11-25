#include <moppe/game/companion_link.hh>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <utility>

namespace moppe::game {
  namespace {
    namespace link_impl {
      using Clock = std::chrono::steady_clock;

      constexpr duration_t speaking_interval = 1.0 / 30.0 * si::second;
      constexpr duration_t calling_interval = 0.5 * si::second;
      constexpr duration_t intro_interval = 1.0 * si::second;

      // "MOPP": tiny peer-introduction datagram for direct UDP meshes.
      constexpr std::uint32_t intro_magic = 0x50504f4dU;
      constexpr std::uint8_t intro_version = 1;

      std::uint64_t random_player_id () {
        std::random_device entropy;
        std::uint64_t id = 0;
        while (id == 0) {
          id = (static_cast<std::uint64_t> (entropy ()) << 32) ^ entropy () ^
               static_cast<std::uint64_t> (
                 Clock::now ().time_since_epoch ().count ());
        }
        return id;
      }

      template <class Pose>
      bool pose_changed (const Pose& a, const Pose& b) {
        if (length2 (position_value (a.position) -
                     position_value (b.position)) > 1e-4f)
          return true;
        if constexpr (requires { a.render_orientation; }) {
          if (length2 (direction_value (a.render_orientation) -
                       direction_value (b.render_orientation)) > 2.5e-5f)
            return true;
        } else {
          if (length2 (direction_value (a.forward) -
                       direction_value (b.forward)) > 2.5e-5f)
            return true;
        }
        if constexpr (requires { a.trailer_position; }) {
          if (length2 (position_value (a.trailer_position) -
                       position_value (b.trailer_position)) > 1e-4f)
            return true;
        }
        return false;
      }

      std::vector<std::uint8_t> encode_intro (
        std::uint32_t world,
        std::span<const std::pair<std::uint32_t, std::uint16_t>> peers) {
        std::vector<std::uint8_t> bytes;
        const std::size_t count = std::min<std::size_t> (peers.size (), 16);
        bytes.reserve (10 + count * 6);
        const auto put_u32 = [&] (std::uint32_t v) {
          bytes.push_back (static_cast<std::uint8_t> (v & 0xFFu));
          bytes.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xFFu));
          bytes.push_back (static_cast<std::uint8_t> ((v >> 16) & 0xFFu));
          bytes.push_back (static_cast<std::uint8_t> ((v >> 24) & 0xFFu));
        };
        const auto put_u16 = [&] (std::uint16_t v) {
          bytes.push_back (static_cast<std::uint8_t> (v & 0xFFu));
          bytes.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xFFu));
        };
        put_u32 (intro_magic);
        bytes.push_back (intro_version);
        put_u32 (world);
        bytes.push_back (static_cast<std::uint8_t> (count));
        for (std::size_t i = 0; i < count; ++i) {
          put_u32 (peers[i].first);
          put_u16 (peers[i].second);
        }
        return bytes;
      }

      bool decode_intro (std::span<const std::uint8_t> bytes,
                         std::uint32_t& world,
                         std::vector<PeerEndpoint>& endpoints) {
        if (bytes.size () < 10)
          return false;
        const auto get_u32 = [&] (std::size_t at) -> std::uint32_t {
          return static_cast<std::uint32_t> (bytes[at]) |
                 (static_cast<std::uint32_t> (bytes[at + 1]) << 8) |
                 (static_cast<std::uint32_t> (bytes[at + 2]) << 16) |
                 (static_cast<std::uint32_t> (bytes[at + 3]) << 24);
        };
        const auto get_u16 = [&] (std::size_t at) -> std::uint16_t {
          return static_cast<std::uint16_t> (bytes[at]) |
                 static_cast<std::uint16_t> (bytes[at + 1] << 8);
        };
        if (get_u32 (0) != intro_magic || bytes[4] != intro_version)
          return false;
        world = get_u32 (5);
        const std::size_t count = bytes[9];
        if (bytes.size () != 10 + count * 6)
          return false;
        for (std::size_t i = 0; i < count; ++i) {
          const std::size_t at = 10 + i * 6;
          endpoints.push_back (
            format_ipv4_endpoint (get_u32 (at), get_u16 (at + 4)));
        }
        return true;
      }
    }
  }

  struct CompanionLink::State {
    struct Peer {
      std::uint64_t player = 0;
      std::uint32_t sequence = 0;
      PeerEndpoint endpoint;
      link_time_t heard_at = link_time (-1e9);
      FleetPoses last_fleet {};
      std::uint16_t seen_fleet_mask = 0;
    };

    std::unique_ptr<PeerTransport> transport;
    ClockFn clock;
    link_impl::Clock::time_point began = link_impl::Clock::now ();
    std::uint64_t player = 0;
    std::uint32_t sequence = 0;
    WorldSummary world {};
    bool world_set = false;
    bool elsewhere = false;
    std::optional<WorldSummary> elsewhere_info;
    link_time_t sent = link_time (-1e9);
    link_time_t last_call = link_time (-1e9);
    link_time_t last_intro = link_time (-1e9);
    std::vector<Peer> peers;
    std::vector<PeerEndpoint> introduced;
    // Outgoing fleet delta state.
    FleetPoses last_sent_fleet {};
    bool has_last_sent = false;
    int parked_cursor = 0;
    int full_sync_needed = 1;

    link_time_t now () const {
      if (clock)
        return clock ();
      return link_time (
        std::chrono::duration<double> (link_impl::Clock::now () - began)
          .count ());
    }

    void expire_peers (link_time_t t) {
      std::erase_if (peers, [t] (const Peer& peer) {
        return t - peer.heard_at >= CompanionTrack::patience;
      });
    }

    bool speaking (link_time_t t) const {
      return std::ranges::any_of (peers, [t] (const Peer& peer) {
        return t - peer.heard_at < CompanionTrack::patience;
      });
    }

    Peer* find_peer (std::uint64_t id) {
      for (Peer& peer : peers)
        if (peer.player == id)
          return &peer;
      return nullptr;
    }

    bool knows_endpoint (const PeerEndpoint& ep) const {
      for (const Peer& peer : peers)
        if (peer.endpoint == ep)
          return true;
      return false;
    }

    std::uint16_t choose_fleet_mask (const CompanionSnapshot& snapshot,
                                     bool force_full) {
      if (force_full || !has_last_sent || full_sync_needed > 0) {
        if (full_sync_needed > 0)
          --full_sync_needed;
        return all_machines_mask;
      }
      std::uint16_t mask = 0;
      if (const std::optional<Machine> driven = driven_machine (snapshot.mode))
        mask |=
          static_cast<std::uint16_t> (1u << static_cast<unsigned> (*driven));
      if (snapshot.sling_hooked)
        mask |= static_cast<std::uint16_t> (
          1u << static_cast<unsigned> (snapshot.sling_load));
      for_each_machine_across (
        [&] (Machine m, const auto& now_pose, const auto& prev_pose) {
          if (link_impl::pose_changed (now_pose, prev_pose))
            mask |=
              static_cast<std::uint16_t> (1u << static_cast<unsigned> (m));
        },
        snapshot.fleet,
        last_sent_fleet);
      // Always refresh two parked machines round-robin so every machine
      // stays fresh across packet loss within ~0.16 s.
      for (int step = 0; step < 2; ++step) {
        mask |= static_cast<std::uint16_t> (1u << parked_cursor);
        parked_cursor = (parked_cursor + 1) % machine_count;
      }
      return mask;
    }

    void send_introductions (link_time_t t) {
      if (transport->shared_channel () || peers.size () < 2 ||
          t - last_intro < link_impl::intro_interval)
        return;
      last_intro = t;
      for (const Peer& recipient : peers) {
        std::vector<std::pair<std::uint32_t, std::uint16_t>> others;
        for (const Peer& other : peers) {
          if (other.player == recipient.player)
            continue;
          std::uint32_t ip = 0;
          std::uint16_t port = 0;
          if (parse_ipv4_endpoint (other.endpoint, ip, port))
            others.emplace_back (ip, port);
        }
        if (others.empty ())
          continue;
        transport->send_to (recipient.endpoint,
                            link_impl::encode_intro (world.tag, others));
      }
    }
  };

  std::unique_ptr<CompanionLink> CompanionLink::open (const Config& config,
                                                      std::string& error) {
    const std::string target = expand_multiplayer_target (config.address);
    std::unique_ptr<PeerTransport> transport;
    if (is_relay_url (target))
      transport = open_relay_transport (target, error);
    else
      transport = open_udp_transport (target, config.port, error);
    if (!transport)
      return nullptr;
    return attach (std::move (transport));
  }

  std::unique_ptr<CompanionLink>
  CompanionLink::attach (std::unique_ptr<PeerTransport> transport,
                         ClockFn clock,
                         std::uint64_t player_override) {
    if (!transport)
      return nullptr;
    auto state = std::make_unique<State> ();
    state->transport = std::move (transport);
    state->clock = std::move (clock);
    state->player =
      player_override != 0 ? player_override : link_impl::random_player_id ();
    return std::unique_ptr<CompanionLink> (
      new CompanionLink (std::move (state)));
  }

  CompanionLink::CompanionLink (std::unique_ptr<State> state)
      : m_state (std::move (state)) {}

  CompanionLink::~CompanionLink () = default;

  std::uint64_t CompanionLink::player () const {
    return m_state->player;
  }

  link_time_t CompanionLink::now () const {
    return m_state->now ();
  }

  bool CompanionLink::due () const {
    const link_time_t t = m_state->now ();
    const bool active = m_state->speaking (t);
    return t - m_state->sent >=
           (active ? link_impl::speaking_interval - 0.004 * si::second
                   : link_impl::calling_interval);
  }

  void CompanionLink::send (CompanionSnapshot snapshot, link_time_t taken) {
    State& state = *m_state;
    const link_time_t t = state.now ();
    state.expire_peers (t);
    snapshot.player = state.player;
    snapshot.sequence = state.sequence++;
    snapshot.sent = taken;
    if (state.world_set) {
      snapshot.world = state.world.tag;
      snapshot.world_info = state.world;
    }
    state.sent = t;

    const bool active = !state.peers.empty ();
    const bool should_call =
      !state.transport->shared_channel () &&
      state.peers.size () < max_companions &&
      t - state.last_call >= link_impl::calling_interval &&
      (!state.transport->named_endpoint ().has_value () || !active);

    const bool send_full = !active || should_call || !state.introduced.empty ();
    snapshot.fleet_mask = state.choose_fleet_mask (snapshot, send_full);
    // Keep full-fleet packets comfortably below 1,200-byte MTU even with
    // picks and camp extras present.
    if (snapshot.fleet_mask == all_machines_mask && snapshot.picks.size () > 16)
      snapshot.picks.resize (16);
    state.last_sent_fleet = snapshot.fleet;
    state.has_last_sent = true;

    const std::vector<std::uint8_t> bytes = encode (snapshot);
    if (state.transport->shared_channel ()) {
      state.transport->broadcast (bytes);
      return;
    }
    for (const State::Peer& peer : state.peers)
      state.transport->send_to (peer.endpoint, bytes);
    if (!state.introduced.empty ()) {
      for (const PeerEndpoint& ep : state.introduced)
        if (!state.knows_endpoint (ep))
          state.transport->send_to (ep, bytes);
      state.introduced.clear ();
    }
    if (should_call) {
      state.last_call = t;
      state.transport->broadcast (bytes);
    }
    state.send_introductions (t);
  }

  void CompanionLink::receive (std::vector<CompanionSnapshot>& arrived) {
    State& state = *m_state;
    const link_time_t t = state.now ();
    state.expire_peers (t);

    std::vector<ReceivedDatagram> incoming;
    incoming.reserve (64);
    state.transport->receive (incoming, 64);

    for (const ReceivedDatagram& packet : incoming) {
      // Handle peer mesh introductions on direct UDP links.
      std::uint32_t intro_world = 0;
      std::vector<PeerEndpoint> intro_peers;
      if (link_impl::decode_intro (packet.bytes, intro_world, intro_peers)) {
        if (intro_world == state.world.tag &&
            !state.transport->shared_channel ()) {
          for (const PeerEndpoint& ep : intro_peers)
            if (!state.knows_endpoint (ep) &&
                state.introduced.size () < max_companions)
              state.introduced.push_back (ep);
        }
        continue;
      }

      std::optional<CompanionSnapshot> snapshot = decode (packet.bytes);
      if (!snapshot || snapshot->player == state.player)
        continue;
      if (snapshot->world != state.world.tag) {
        state.elsewhere = true;
        state.elsewhere_info = snapshot->world_info;
        continue;
      }

      // On direct UDP, if a peer restarted from the same address with a
      // new player ID, retire their old slot immediately.
      if (!state.transport->shared_channel ()) {
        std::erase_if (state.peers, [&] (const State::Peer& existing) {
          return existing.endpoint == packet.from &&
                 existing.player != snapshot->player;
        });
      }

      State::Peer* peer = state.find_peer (snapshot->player);
      if (!peer) {
        if (state.peers.size () >= max_companions)
          continue;
        state.peers.push_back ({
          .player = snapshot->player,
          .sequence = snapshot->sequence,
          .endpoint = packet.from,
          .heard_at = t,
        });
        peer = &state.peers.back ();
        // Greet the newcomer promptly with a full-fleet snapshot and
        // introduce them to any existing UDP peers right away.
        state.full_sync_needed = 2;
        state.last_intro = link_time (-1e9);
      } else {
        // Overtaken on the way: drop snapshots older than the last taken.
        if (static_cast<std::int32_t> (snapshot->sequence - peer->sequence) <=
            0)
          continue;
        peer->sequence = snapshot->sequence;
        peer->endpoint = packet.from;
        peer->heard_at = t;
      }

      merge_fleet_poses (
        peer->last_fleet, snapshot->fleet, snapshot->fleet_mask);
      peer->seen_fleet_mask |= snapshot->fleet_mask;
      if (peer->seen_fleet_mask != all_machines_mask)
        continue;
      snapshot->fleet = peer->last_fleet;
      snapshot->fleet_mask = all_machines_mask;
      arrived.push_back (std::move (*snapshot));
    }
  }

  void CompanionLink::set_world (const WorldSummary& world) {
    State& state = *m_state;
    if (state.world_set && state.world.tag == world.tag) {
      state.world = world;
      return;
    }
    state.world = world;
    state.world_set = true;
    state.peers.clear ();
    state.introduced.clear ();
    state.elsewhere = false;
    state.elsewhere_info.reset ();
    state.has_last_sent = false;
    state.full_sync_needed = 1;
    state.last_call = link_time (-1e9);
    state.transport->flush ();
  }

  void CompanionLink::set_world (std::uint32_t world) {
    WorldSummary summary = m_state->world;
    summary.tag = world;
    set_world (summary);
  }

  bool CompanionLink::heard_elsewhere () {
    return std::exchange (m_state->elsewhere, false);
  }

  std::optional<WorldSummary> CompanionLink::elsewhere_world () {
    return std::exchange (m_state->elsewhere_info, std::nullopt);
  }

  std::size_t CompanionLink::peer_count () const {
    const link_time_t t = m_state->now ();
    std::size_t count = 0;
    for (const State::Peer& peer : m_state->peers)
      if (t - peer.heard_at < CompanionTrack::patience)
        ++count;
    return count;
  }

  std::string CompanionLink::address_of (std::uint64_t player_id) const {
    const link_time_t t = m_state->now ();
    for (const State::Peer& peer : m_state->peers)
      if (peer.player == player_id &&
          t - peer.heard_at < CompanionTrack::patience)
        return peer.endpoint.key;
    return {};
  }

  std::string CompanionLink::companion_address () const {
    const link_time_t t = m_state->now ();
    for (const State::Peer& peer : m_state->peers)
      if (t - peer.heard_at < CompanionTrack::patience)
        return peer.endpoint.key;
    return {};
  }

  std::uint16_t CompanionLink::port () const {
    return m_state->transport->local_port ();
  }

  bool CompanionLink::ready () const {
    return m_state->transport->ready ();
  }

  std::string CompanionLink::transport_description () const {
    return m_state->transport->description ();
  }

  std::string CompanionLink::last_error () const {
    return m_state->transport->last_error ();
  }
}
