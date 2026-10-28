#include <moppe/game/companion_link.hh>

#include <chrono>
#include <random>

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
#define MOPPE_COMPANION_SOCKETS 1
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#define MOPPE_COMPANION_SOCKETS 0
#endif

namespace moppe::game {
  namespace {
    using Clock = std::chrono::steady_clock;

    constexpr double speaking_interval = 1.0 / 30.0;
    constexpr double calling_interval = 0.5;
  }

#if MOPPE_COMPANION_SOCKETS
  namespace {
    sockaddr_in address_of (in_addr_t host, std::uint16_t port) {
      sockaddr_in address {};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = host;
      address.sin_port = htons (port);
      return address;
    }

    std::string text_of (const sockaddr_in& address) {
      char host[INET_ADDRSTRLEN] = "?";
      ::inet_ntop (AF_INET, &address.sin_addr, host, sizeof host);
      return std::string (host) + ":" +
             std::to_string (ntohs (address.sin_port));
    }

    // `host` or `host:port`, by name or by number.
    bool resolve (const std::string& text,
                  std::uint16_t default_port,
                  sockaddr_in& address,
                  std::string& error) {
      std::string host = text;
      std::string port = std::to_string (default_port);
      if (const std::size_t colon = text.rfind (':');
          colon != std::string::npos) {
        host = text.substr (0, colon);
        port = text.substr (colon + 1);
      }
      addrinfo hints {};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      addrinfo* found = nullptr;
      if (const int failure =
            ::getaddrinfo (host.c_str (), port.c_str (), &hints, &found);
          failure != 0 || !found) {
        error = "cannot find " + text + ": " + ::gai_strerror (failure);
        return false;
      }
      std::memcpy (&address, found->ai_addr, sizeof address);
      ::freeaddrinfo (found);
      return true;
    }
  }

  struct CompanionLink::Socket {
    int fd = -1;
    std::uint16_t port = 0;
    std::uint16_t calling_port = default_port;
    std::uint64_t player = 0;
    std::uint32_t sequence = 0;
    Clock::time_point began = Clock::now ();
    double sent = -1e9;
    // The machine named at launch, if one was.
    bool named = false;
    sockaddr_in named_address {};
    // The companion, once heard.
    bool heard = false;
    std::uint64_t companion = 0;
    sockaddr_in companion_address {};
    double heard_at = -1e9;

    ~Socket () {
      if (fd >= 0)
        ::close (fd);
    }

    double now () const {
      return std::chrono::duration<double> (Clock::now () - began).count ();
    }

    bool speaking () const {
      return heard && now () - heard_at < CompanionTrack::patience;
    }

    void send_to (const sockaddr_in& to,
                  const std::vector<std::uint8_t>& bytes) const {
      // A lost or refused datagram is only a missed snapshot.
      (void)::sendto (fd,
                      bytes.data (),
                      bytes.size (),
                      0,
                      reinterpret_cast<const sockaddr*> (&to),
                      sizeof to);
    }

    // Calls out to everyone who might be listening: every network this
    // machine is on, and this machine itself.
    void call (const std::vector<std::uint8_t>& bytes) const {
      send_to (address_of (htonl (INADDR_BROADCAST), calling_port), bytes);
      send_to (address_of (htonl (INADDR_LOOPBACK), calling_port), bytes);
      ifaddrs* interfaces = nullptr;
      if (::getifaddrs (&interfaces) != 0)
        return;
      for (const ifaddrs* each = interfaces; each; each = each->ifa_next) {
        if (!each->ifa_addr || each->ifa_addr->sa_family != AF_INET ||
            !(each->ifa_flags & IFF_BROADCAST) || !(each->ifa_flags & IFF_UP) ||
            !each->ifa_broadaddr)
          continue;
        sockaddr_in everyone {};
        std::memcpy (&everyone, each->ifa_broadaddr, sizeof everyone);
        everyone.sin_port = htons (calling_port);
        send_to (everyone, bytes);
      }
      ::freeifaddrs (interfaces);
    }
  };

  std::unique_ptr<CompanionLink> CompanionLink::open (const Config& config,
                                                      std::string& error) {
    auto socket = std::make_unique<Socket> ();
    socket->calling_port = config.port;
    if (!config.address.empty ()) {
      if (!resolve (config.address, default_port, socket->named_address, error))
        return nullptr;
      socket->named = true;
    }
    socket->fd = ::socket (AF_INET, SOCK_DGRAM, 0);
    if (socket->fd < 0) {
      error = std::string ("no socket: ") + std::strerror (errno);
      return nullptr;
    }
    const int yes = 1;
    ::setsockopt (socket->fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    ::fcntl (
      socket->fd, F_SETFL, ::fcntl (socket->fd, F_GETFL, 0) | O_NONBLOCK);

    sockaddr_in here = address_of (htonl (INADDR_ANY), config.port);
    if (::bind (socket->fd, reinterpret_cast<sockaddr*> (&here), sizeof here) !=
        0) {
      // Another game on this machine has the port; any other will do.
      here.sin_port = 0;
      if (::bind (socket->fd,
                  reinterpret_cast<sockaddr*> (&here),
                  sizeof here) != 0) {
        error = std::string ("cannot listen: ") + std::strerror (errno);
        return nullptr;
      }
    }
    socklen_t size = sizeof here;
    ::getsockname (socket->fd, reinterpret_cast<sockaddr*> (&here), &size);
    socket->port = ntohs (here.sin_port);

    std::random_device entropy;
    socket->player =
      (static_cast<std::uint64_t> (entropy ()) << 32) ^ entropy () ^
      static_cast<std::uint64_t> (Clock::now ().time_since_epoch ().count ());
    return std::unique_ptr<CompanionLink> (
      new CompanionLink (std::move (socket)));
  }

  void CompanionLink::send (CompanionSnapshot snapshot, double taken) {
    Socket& socket = *m_socket;
    snapshot.player = socket.player;
    snapshot.sequence = socket.sequence++;
    snapshot.sent = taken;
    socket.sent = socket.now ();
    const std::vector<std::uint8_t> bytes = encode (snapshot);
    if (socket.speaking ())
      socket.send_to (socket.companion_address, bytes);
    else if (socket.named)
      socket.send_to (socket.named_address, bytes);
    else
      socket.call (bytes);
  }

  void CompanionLink::receive (std::vector<CompanionSnapshot>& arrived) {
    Socket& socket = *m_socket;
    std::uint8_t bytes[2048];
    // No more than a burst at a time: a flood must not hold up the frame.
    for (int taken = 0; taken < 64; ++taken) {
      sockaddr_in from {};
      socklen_t size = sizeof from;
      const ssize_t length = ::recvfrom (socket.fd,
                                         bytes,
                                         sizeof bytes,
                                         0,
                                         reinterpret_cast<sockaddr*> (&from),
                                         &size);
      if (length < 0)
        break;
      std::optional<CompanionSnapshot> snapshot =
        decode (std::span (bytes, static_cast<std::size_t> (length)));
      if (!snapshot || snapshot->player == socket.player)
        continue;
      if (socket.speaking () && snapshot->player != socket.companion)
        continue;
      socket.heard = true;
      socket.companion = snapshot->player;
      socket.companion_address = from;
      socket.heard_at = socket.now ();
      arrived.push_back (std::move (*snapshot));
    }
  }

  std::string CompanionLink::companion_address () const {
    return m_socket->speaking () ? text_of (m_socket->companion_address)
                                 : std::string ();
  }
#else
  struct CompanionLink::Socket {
    std::uint16_t port = 0;
    std::uint64_t player = 0;
    double sent = 0.0;
    Clock::time_point began = Clock::now ();

    double now () const {
      return std::chrono::duration<double> (Clock::now () - began).count ();
    }
    bool speaking () const {
      return false;
    }
  };

  std::unique_ptr<CompanionLink> CompanionLink::open (const Config&,
                                                      std::string& error) {
    error = "this platform has no way to reach another player yet";
    return nullptr;
  }

  void CompanionLink::send (CompanionSnapshot, double) {}
  void CompanionLink::receive (std::vector<CompanionSnapshot>&) {}

  std::string CompanionLink::companion_address () const {
    return {};
  }
#endif

  CompanionLink::CompanionLink (std::unique_ptr<Socket> socket)
      : m_socket (std::move (socket)) {}

  CompanionLink::~CompanionLink () = default;

  std::uint64_t CompanionLink::player () const {
    return m_socket->player;
  }

  double CompanionLink::now () const {
    return m_socket->now ();
  }

  bool CompanionLink::due () const {
    // A little under the interval, so that a game drawing sixty frames
    // a second speaks on every other one and not now on the second, now
    // on the third.
    return m_socket->now () - m_socket->sent >= (m_socket->speaking ()
                                                   ? speaking_interval - 0.004
                                                   : calling_interval);
  }

  std::uint16_t CompanionLink::port () const {
    return m_socket->port;
  }
}
