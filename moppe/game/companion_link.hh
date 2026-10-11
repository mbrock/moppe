#ifndef MOPPE_GAME_COMPANION_LINK_HH
#define MOPPE_GAME_COMPANION_LINK_HH

// The wire between two players (game/companion.hh): one UDP socket that
// sends this player's snapshots and takes in the other's. Neither side
// serves the other. Each calls out on the local network until it hears
// someone, then speaks to them alone; `address` names the other machine
// outright where a network does not carry broadcasts.

#include <moppe/game/companion.hh>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace moppe::game {
  class CompanionLink {
  public:
    static constexpr std::uint16_t default_port = 27960;

    struct Config {
      // The other machine, as `host` or `host:port`; empty to find
      // whoever is playing on the local network.
      std::string address;
      // The port to listen on. Taken already (a second game on the same
      // machine), any free one serves: the other side answers whoever
      // called.
      std::uint16_t port = default_port;
    };

    // Nothing, with the reason in `error`, where the socket cannot be
    // had or the platform has none.
    static std::unique_ptr<CompanionLink> open (const Config& config,
                                                std::string& error);

    ~CompanionLink ();
    CompanionLink (const CompanionLink&) = delete;
    CompanionLink& operator= (const CompanionLink&) = delete;

    // This player's number, and the link's own clock in seconds.
    std::uint64_t player () const;
    double now () const;

    // Whether it is time to say something: thirty times a second to a
    // companion, once a second while calling out for one.
    bool due () const;
    // Stamps the snapshot with this player, the next sequence number,
    // and the time, and sends it.
    void send (CompanionSnapshot snapshot);

    // Every snapshot that has arrived from the companion since the last
    // call, oldest first. The first player heard becomes the companion
    // and stays so until they fall silent.
    void receive (std::vector<CompanionSnapshot>& arrived);

    // Whom this link is speaking to, for the log; empty while calling.
    std::string companion_address () const;
    std::uint16_t port () const;

  private:
    struct Socket;
    explicit CompanionLink (std::unique_ptr<Socket> socket);
    std::unique_ptr<Socket> m_socket;
  };
}

#endif
