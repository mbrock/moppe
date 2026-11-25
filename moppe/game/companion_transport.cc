#include <moppe/environment.hh>
#include <moppe/game/companion_transport.hh>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <sstream>
#include <utility>

#if defined(__EMSCRIPTEN__)
#define MOPPE_NET_WEB 1
#define MOPPE_NET_POSIX 0
#define MOPPE_NET_WIN32 0
#include <emscripten.h>
#elif defined(_WIN32)
#define MOPPE_NET_WEB 0
#define MOPPE_NET_POSIX 0
#define MOPPE_NET_WIN32 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__unix__) || defined(__APPLE__)
#define MOPPE_NET_WEB 0
#define MOPPE_NET_POSIX 1
#define MOPPE_NET_WIN32 0
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#define MOPPE_NET_WEB 0
#define MOPPE_NET_POSIX 0
#define MOPPE_NET_WIN32 0
#endif

#if MOPPE_NET_WEB
// Browser WebSocket bridge via EM_JS (avoids preprocessor comma/quote hazards).
EM_JS (int, moppe_ws_open, (const char* url_ptr), {
  const url = UTF8ToString (url_ptr);
  const table =
    (Module._moppeWs = Module._moppeWs || { next : 1, sockets : {} });
  const id = table.next++;
  const entry = { ws : null, open : false, error : "", inbox : [] };
  try {
    const ws = new WebSocket (url);
    ws.binaryType = "arraybuffer";
    ws.onopen = function () {
      entry.open = true;
    };
    ws.onmessage = function (ev) {
      if (ev.data instanceof ArrayBuffer) {
        if (entry.inbox.length >= 128)
          entry.inbox.shift ();
        entry.inbox.push (new Uint8Array (ev.data));
      }
    };
    ws.onerror = function () {
      if (!entry.error)
        entry.error = "relay connection failed";
    };
    ws.onclose = function () {
      entry.open = false;
      if (!entry.error)
        entry.error = "relay closed the connection";
    };
    entry.ws = ws;
  } catch (err) {
    entry.error = String (err);
  }
  table.sockets[id] = entry;
  return id;
});

EM_JS (void, moppe_ws_close, (int id), {
  const table = Module._moppeWs;
  if (!table || !table.sockets[id])
    return;
  const entry = table.sockets[id];
  if (entry.ws) {
    try {
      entry.ws.close ();
    } catch (_) {}
  }
  delete table.sockets[id];
});

EM_JS (void, moppe_ws_send, (int id, const std::uint8_t* ptr, int len), {
  const table = Module._moppeWs;
  const entry = table && table.sockets[id];
  if (!entry || !entry.open || !entry.ws || entry.ws.readyState != = 1)
    return;
  entry.ws.send (HEAPU8.slice (ptr, ptr + len));
});

EM_JS (int, moppe_ws_recv, (int id, std::uint8_t* dst, int cap), {
  const table = Module._moppeWs;
  const entry = table && table.sockets[id];
  if (!entry || entry.inbox.length == = 0)
    return -1;
  const msg = entry.inbox.shift ();
  if (msg.byteLength > cap)
    return 0;
  HEAPU8.set (msg, dst);
  return msg.byteLength;
});

EM_JS (void, moppe_ws_flush, (int id), {
  const table = Module._moppeWs;
  const entry = table && table.sockets[id];
  if (entry)
    entry.inbox.length = 0;
});

EM_JS (int, moppe_ws_ready, (int id), {
  const table = Module._moppeWs;
  const entry = table && table.sockets[id];
  return entry && entry.open ? 1 : 0;
});

EM_JS (int, moppe_ws_error, (int id, char* dst, int cap), {
  const table = Module._moppeWs;
  const entry = table && table.sockets[id];
  if (!entry || !entry.error)
    return 0;
  return stringToUTF8 (entry.error, dst, cap);
});
#endif

namespace moppe::game {
  namespace {
    namespace net_transport {
      constexpr std::size_t max_datagram_bytes = 4096;
      constexpr std::size_t max_ws_frame_bytes = 16384;

      std::string trim_copy (std::string_view text) {
        std::size_t begin = 0;
        std::size_t end = text.size ();
        while (begin < end &&
               std::isspace (static_cast<unsigned char> (text[begin])))
          ++begin;
        while (end > begin &&
               std::isspace (static_cast<unsigned char> (text[end - 1])))
          --end;
        return std::string (text.substr (begin, end - begin));
      }

#if MOPPE_NET_POSIX || MOPPE_NET_WIN32
      std::string base64_encode (std::span<const std::uint8_t> data) {
        static constexpr char table[] =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve (((data.size () + 2) / 3) * 4);
        for (std::size_t i = 0; i < data.size (); i += 3) {
          const std::uint32_t b0 = data[i];
          const std::uint32_t b1 = i + 1 < data.size () ? data[i + 1] : 0;
          const std::uint32_t b2 = i + 2 < data.size () ? data[i + 2] : 0;
          const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
          out.push_back (table[(triple >> 18) & 0x3F]);
          out.push_back (table[(triple >> 12) & 0x3F]);
          out.push_back (i + 1 < data.size () ? table[(triple >> 6) & 0x3F]
                                              : '=');
          out.push_back (i + 2 < data.size () ? table[triple & 0x3F] : '=');
        }
        return out;
      }
#if MOPPE_NET_WIN32
      using SocketHandle = SOCKET;
      constexpr SocketHandle invalid_socket = INVALID_SOCKET;

      bool ensure_winsock (std::string& error) {
        static const int status = [] {
          WSADATA wsa {};
          return ::WSAStartup (MAKEWORD (2, 2), &wsa);
        }();
        if (status != 0) {
          error = "Winsock unavailable (" + std::to_string (status) + ")";
          return false;
        }
        return true;
      }

      void close_socket (SocketHandle fd) {
        if (fd != invalid_socket)
          ::closesocket (fd);
      }

      bool set_nonblocking (SocketHandle fd) {
        u_long mode = 1;
        return ::ioctlsocket (fd, FIONBIO, &mode) == 0;
      }

      bool last_would_block () {
        const int err = ::WSAGetLastError ();
        return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS ||
               err == WSAEALREADY;
      }

      std::string last_socket_error () {
        return "error " + std::to_string (::WSAGetLastError ());
      }
#else
      using SocketHandle = int;
      constexpr SocketHandle invalid_socket = -1;

      bool ensure_winsock (std::string&) {
        return true;
      }

      void close_socket (SocketHandle fd) {
        if (fd >= 0)
          ::close (fd);
      }

      bool set_nonblocking (SocketHandle fd) {
        const int flags = ::fcntl (fd, F_GETFL, 0);
        return flags >= 0 && ::fcntl (fd, F_SETFL, flags | O_NONBLOCK) == 0;
      }

      bool last_would_block () {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
      }

      std::string last_socket_error () {
        return std::strerror (errno);
      }
#endif

      sockaddr_in ipv4_sockaddr (std::uint32_t ip_host, std::uint16_t port) {
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl (ip_host);
        address.sin_port = htons (port);
        return address;
      }

      PeerEndpoint endpoint_of (const sockaddr_in& address) {
        return format_ipv4_endpoint (ntohl (address.sin_addr.s_addr),
                                     ntohs (address.sin_port));
      }

      bool resolve_ipv4 (const std::string& text,
                         std::uint16_t fallback_port,
                         sockaddr_in& address,
                         std::string& error) {
        std::string host = text;
        std::string port = std::to_string (fallback_port);
        if (const std::size_t colon = text.rfind (':');
            colon != std::string::npos && text.find (':') == colon) {
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
          error = "cannot find " + text;
          return false;
        }
        std::memcpy (&address, found->ai_addr, sizeof address);
        ::freeaddrinfo (found);
        return true;
      }

      class UdpTransport final : public PeerTransport {
      public:
        UdpTransport (SocketHandle fd,
                      std::uint16_t bound_port,
                      std::uint16_t calling_port,
                      std::optional<sockaddr_in> named)
            : m_fd (fd), m_port (bound_port), m_calling_port (calling_port),
              m_named (named) {}

        ~UdpTransport () override {
          close_socket (m_fd);
        }

        void send_to (const PeerEndpoint& to,
                      std::span<const std::uint8_t> bytes) override {
          std::uint32_t ip = 0;
          std::uint16_t port = 0;
          if (!parse_ipv4_endpoint (to, ip, port))
            return;
          send_raw (ipv4_sockaddr (ip, port), bytes);
        }

        void broadcast (std::span<const std::uint8_t> bytes) override {
          if (m_named) {
            send_raw (*m_named, bytes);
            return;
          }
          std::set<std::uint32_t> targets;
          targets.insert (INADDR_BROADCAST);
          if (m_port != m_calling_port)
            targets.insert (INADDR_LOOPBACK);
#if MOPPE_NET_POSIX
          ifaddrs* interfaces = nullptr;
          if (::getifaddrs (&interfaces) == 0) {
            for (const ifaddrs* each = interfaces; each;
                 each = each->ifa_next) {
              if (!each->ifa_addr || each->ifa_addr->sa_family != AF_INET ||
                  !(each->ifa_flags & IFF_BROADCAST) ||
                  !(each->ifa_flags & IFF_UP) || !each->ifa_broadaddr)
                continue;
              sockaddr_in broad {};
              std::memcpy (&broad, each->ifa_broadaddr, sizeof broad);
              targets.insert (ntohl (broad.sin_addr.s_addr));
            }
            ::freeifaddrs (interfaces);
          }
#elif MOPPE_NET_WIN32
          ULONG needed = 16384;
          std::vector<std::uint8_t> storage (needed);
          auto* adapters =
            reinterpret_cast<PIP_ADAPTER_ADDRESSES> (storage.data ());
          if (::GetAdaptersAddresses (AF_INET,
                                      GAA_FLAG_SKIP_ANYCAST |
                                        GAA_FLAG_SKIP_MULTICAST |
                                        GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr,
                                      adapters,
                                      &needed) == ERROR_SUCCESS) {
            for (PIP_ADAPTER_ADDRESSES a = adapters; a; a = a->Next) {
              if (a->OperStatus != IfOperStatusUp)
                continue;
              for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u;
                   u = u->Next) {
                if (!u->Address.lpSockaddr ||
                    u->Address.lpSockaddr->sa_family != AF_INET ||
                    u->OnLinkPrefixLength > 32)
                  continue;
                const auto* sin =
                  reinterpret_cast<const sockaddr_in*> (u->Address.lpSockaddr);
                const std::uint32_t ip = ntohl (sin->sin_addr.s_addr);
                const std::uint32_t mask =
                  u->OnLinkPrefixLength == 0
                    ? 0u
                    : (~0u << (32u - u->OnLinkPrefixLength));
                targets.insert (ip | ~mask);
              }
            }
          }
#endif
          for (const std::uint32_t ip : targets)
            send_raw (ipv4_sockaddr (ip, m_calling_port), bytes);
        }

        void receive (std::vector<ReceivedDatagram>& out,
                      std::size_t limit) override {
          std::uint8_t buffer[max_datagram_bytes];
          for (std::size_t taken = 0; taken < limit; ++taken) {
            sockaddr_in from {};
            socklen_t size = sizeof from;
            const auto length = ::recvfrom (m_fd,
                                            reinterpret_cast<char*> (buffer),
                                            static_cast<int> (sizeof buffer),
                                            0,
                                            reinterpret_cast<sockaddr*> (&from),
                                            &size);
            if (length <= 0)
              break;
            ReceivedDatagram packet;
            packet.from = endpoint_of (from);
            packet.bytes.assign (buffer, buffer + length);
            out.push_back (std::move (packet));
          }
        }

        void flush () override {
          std::uint8_t discard[max_datagram_bytes];
          for (int i = 0; i < 256; ++i) {
            sockaddr_in from {};
            socklen_t size = sizeof from;
            if (::recvfrom (m_fd,
                            reinterpret_cast<char*> (discard),
                            static_cast<int> (sizeof discard),
                            0,
                            reinterpret_cast<sockaddr*> (&from),
                            &size) <= 0)
              break;
          }
        }

        std::string description () const override {
          if (m_named)
            return "UDP :" + std::to_string (m_port) + " -> " +
                   endpoint_of (*m_named).key;
          return "LAN UDP :" + std::to_string (m_port);
        }

        std::uint16_t local_port () const override {
          return m_port;
        }

        std::optional<PeerEndpoint> named_endpoint () const override {
          if (!m_named)
            return std::nullopt;
          return endpoint_of (*m_named);
        }

      private:
        void send_raw (const sockaddr_in& to,
                       std::span<const std::uint8_t> bytes) const {
          (void)::sendto (m_fd,
                          reinterpret_cast<const char*> (bytes.data ()),
                          static_cast<int> (bytes.size ()),
                          0,
                          reinterpret_cast<const sockaddr*> (&to),
                          sizeof to);
        }

        SocketHandle m_fd = invalid_socket;
        std::uint16_t m_port = 0;
        std::uint16_t m_calling_port = 0;
        std::optional<sockaddr_in> m_named;
      };

      // Minimal non-blocking RFC 6455 binary WebSocket client over TCP
      // (`ws://`).
      class TcpWebSocketTransport final : public PeerTransport {
      public:
        TcpWebSocketTransport (SocketHandle fd, RelayUrl url)
            : m_fd (fd), m_url (std::move (url)),
              m_rng (std::random_device {}()) {
          pump ();
        }

        ~TcpWebSocketTransport () override {
          if (m_state == State::Open) {
            // Best-effort close frame.
            const std::uint8_t empty[1] {};
            queue_frame (0x8, std::span<const std::uint8_t> (empty, 0));
            pump_write ();
          }
          close_socket (m_fd);
        }

        void send_to (const PeerEndpoint&,
                      std::span<const std::uint8_t> bytes) override {
          broadcast (bytes);
        }

        void broadcast (std::span<const std::uint8_t> bytes) override {
          pump ();
          if (m_state != State::Open || bytes.size () > max_ws_frame_bytes)
            return;
          // Drop oldest stale outbound frames if the socket stalls.
          if (m_write_buf.size () > 65536)
            return;
          queue_frame (0x2, bytes);
          pump_write ();
        }

        void receive (std::vector<ReceivedDatagram>& out,
                      std::size_t limit) override {
          pump ();
          std::size_t taken = 0;
          while (!m_inbox.empty () && taken < limit) {
            out.push_back (
              { .from = { "relay" }, .bytes = std::move (m_inbox.front ()) });
            m_inbox.pop_front ();
            ++taken;
          }
        }

        void flush () override {
          pump ();
          m_inbox.clear ();
        }

        bool shared_channel () const override {
          return true;
        }

        bool ready () const override {
          const_cast<TcpWebSocketTransport*> (this)->pump ();
          return m_state == State::Open;
        }

        std::string description () const override {
          return m_url.full;
        }

        std::string last_error () const override {
          return m_error;
        }

      private:
        enum class State { Connecting, Handshaking, Open, Closed };

        void fail (const std::string& reason) {
          if (m_error.empty ())
            m_error = reason;
          m_state = State::Closed;
          close_socket (m_fd);
          m_fd = invalid_socket;
        }

        void begin_handshake () {
          std::uint8_t nonce[16];
          for (std::uint8_t& byte : nonce)
            byte = static_cast<std::uint8_t> (m_rng ());
          const std::string key = base64_encode (nonce);
          std::ostringstream req;
          req << "GET " << m_url.path << " HTTP/1.1\r\n"
              << "Host: " << m_url.host << ":" << m_url.port << "\r\n"
              << "Upgrade: websocket\r\n"
              << "Connection: Upgrade\r\n"
              << "Sec-WebSocket-Key: " << key << "\r\n"
              << "Sec-WebSocket-Version: 13\r\n\r\n";
          const std::string text = req.str ();
          m_write_buf.insert (m_write_buf.end (), text.begin (), text.end ());
          m_state = State::Handshaking;
          pump_write ();
        }

        void queue_frame (std::uint8_t opcode,
                          std::span<const std::uint8_t> payload) {
          m_write_buf.push_back (static_cast<std::uint8_t> (0x80u | opcode));
          const std::size_t n = payload.size ();
          if (n < 126) {
            m_write_buf.push_back (static_cast<std::uint8_t> (0x80u | n));
          } else {
            m_write_buf.push_back (static_cast<std::uint8_t> (0x80u | 126u));
            m_write_buf.push_back (static_cast<std::uint8_t> ((n >> 8) & 0xFF));
            m_write_buf.push_back (static_cast<std::uint8_t> (n & 0xFF));
          }
          std::uint8_t mask[4];
          const std::uint32_t bits = m_rng ();
          std::memcpy (mask, &bits, 4);
          m_write_buf.insert (m_write_buf.end (), mask, mask + 4);
          const std::size_t start = m_write_buf.size ();
          m_write_buf.resize (start + n);
          for (std::size_t i = 0; i < n; ++i)
            m_write_buf[start + i] = payload[i] ^ mask[i & 3u];
        }

        void pump_write () {
          while (m_fd != invalid_socket && !m_write_buf.empty ()) {
            const auto sent =
              ::send (m_fd,
                      reinterpret_cast<const char*> (m_write_buf.data ()),
                      static_cast<int> (m_write_buf.size ()),
                      0);
            if (sent > 0) {
              m_write_buf.erase (m_write_buf.begin (),
                                 m_write_buf.begin () + sent);
              continue;
            }
            if (sent < 0 && last_would_block ())
              return;
            fail ("relay connection lost");
            return;
          }
        }

        void parse_frames () {
          while (m_read_buf.size () >= 2) {
            const std::uint8_t b0 = m_read_buf[0];
            const std::uint8_t b1 = m_read_buf[1];
            const bool fin = (b0 & 0x80u) != 0;
            const std::uint8_t opcode = b0 & 0x0Fu;
            const bool masked = (b1 & 0x80u) != 0;
            std::uint64_t length = b1 & 0x7Fu;
            std::size_t pos = 2;
            if (length == 126) {
              if (m_read_buf.size () < pos + 2)
                return;
              length = (static_cast<std::uint64_t> (m_read_buf[pos]) << 8) |
                       m_read_buf[pos + 1];
              pos += 2;
            } else if (length == 127) {
              if (m_read_buf.size () < pos + 8)
                return;
              length = 0;
              for (int i = 0; i < 8; ++i)
                length = (length << 8) | m_read_buf[pos + i];
              pos += 8;
            }
            if (length > max_ws_frame_bytes) {
              fail ("relay sent oversized frame");
              return;
            }
            std::uint8_t mask[4] {};
            if (masked) {
              if (m_read_buf.size () < pos + 4)
                return;
              std::memcpy (mask, m_read_buf.data () + pos, 4);
              pos += 4;
            }
            if (m_read_buf.size () < pos + length)
              return;
            std::vector<std::uint8_t> payload (
              m_read_buf.begin () + static_cast<std::ptrdiff_t> (pos),
              m_read_buf.begin () + static_cast<std::ptrdiff_t> (pos + length));
            if (masked)
              for (std::size_t i = 0; i < payload.size (); ++i)
                payload[i] ^= mask[i & 3u];
            m_read_buf.erase (m_read_buf.begin (),
                              m_read_buf.begin () +
                                static_cast<std::ptrdiff_t> (pos + length));

            if (opcode == 0x8) {
              fail ("relay closed the room");
              return;
            }
            if (opcode == 0x9) {
              queue_frame (0xA, payload);
              pump_write ();
              continue;
            }
            if (opcode == 0xA)
              continue;
            if (opcode == 0x2 || opcode == 0x0) {
              if (opcode == 0x2)
                m_fragment.clear ();
              if (m_fragment.size () + payload.size () <= max_ws_frame_bytes)
                m_fragment.insert (
                  m_fragment.end (), payload.begin (), payload.end ());
              if (fin) {
                if (m_inbox.size () >= 128)
                  m_inbox.pop_front ();
                m_inbox.push_back (std::move (m_fragment));
                m_fragment.clear ();
              }
            }
          }
        }

        void pump () {
          if (m_state == State::Closed || m_fd == invalid_socket)
            return;
          if (m_state == State::Connecting) {
            fd_set writable, failed;
            FD_ZERO (&writable);
            FD_ZERO (&failed);
            FD_SET (m_fd, &writable);
            FD_SET (m_fd, &failed);
            timeval instant { 0, 0 };
            const int ready_count = ::select (static_cast<int> (m_fd) + 1,
                                              nullptr,
                                              &writable,
                                              &failed,
                                              &instant);
            if (ready_count <= 0)
              return;
            int sock_err = 0;
            socklen_t len = sizeof sock_err;
            ::getsockopt (m_fd,
                          SOL_SOCKET,
                          SO_ERROR,
                          reinterpret_cast<char*> (&sock_err),
                          &len);
            if (sock_err != 0 || FD_ISSET (m_fd, &failed)) {
              fail ("cannot reach relay " + m_url.host + ":" +
                    std::to_string (m_url.port));
              return;
            }
            if (FD_ISSET (m_fd, &writable))
              begin_handshake ();
          }
          pump_write ();
          if (m_state == State::Closed)
            return;
          std::uint8_t chunk[4096];
          for (int i = 0; i < 16; ++i) {
            const auto got = ::recv (m_fd,
                                     reinterpret_cast<char*> (chunk),
                                     static_cast<int> (sizeof chunk),
                                     0);
            if (got > 0) {
              m_read_buf.insert (m_read_buf.end (), chunk, chunk + got);
              continue;
            }
            if (got == 0) {
              fail ("relay closed the connection");
              return;
            }
            if (last_would_block ())
              break;
            fail ("relay read failed");
            return;
          }
          if (m_state == State::Handshaking) {
            static constexpr std::uint8_t end_marker[4] = {
              '\r', '\n', '\r', '\n'
            };
            const auto end = std::search (m_read_buf.begin (),
                                          m_read_buf.end (),
                                          end_marker,
                                          end_marker + 4);
            if (end == m_read_buf.end ()) {
              if (m_read_buf.size () > 8192)
                fail ("relay handshake too large");
              return;
            }
            const std::string header (m_read_buf.begin (), end);
            if (header.find (" 101 ") == std::string::npos) {
              fail ("relay rejected WebSocket upgrade");
              return;
            }
            m_read_buf.erase (m_read_buf.begin (), end + 4);
            m_state = State::Open;
          }
          if (m_state == State::Open)
            parse_frames ();
        }

        SocketHandle m_fd = invalid_socket;
        RelayUrl m_url;
        State m_state = State::Connecting;
        std::mt19937 m_rng;
        std::vector<std::uint8_t> m_write_buf;
        std::vector<std::uint8_t> m_read_buf;
        std::vector<std::uint8_t> m_fragment;
        std::deque<std::vector<std::uint8_t>> m_inbox;
        std::string m_error;
      };
#endif

#if MOPPE_NET_WEB
      // Browser WebSocket relay transport, driven through the page's own
      // WebSocket object (no extra Emscripten link libraries needed).
      class WebRelayTransport final : public PeerTransport {
      public:
        explicit WebRelayTransport (std::string url) : m_url (std::move (url)) {
          m_handle = moppe_ws_open (m_url.c_str ());
        }

        ~WebRelayTransport () override {
          moppe_ws_close (m_handle);
        }

        void send_to (const PeerEndpoint&,
                      std::span<const std::uint8_t> bytes) override {
          broadcast (bytes);
        }

        void broadcast (std::span<const std::uint8_t> bytes) override {
          if (bytes.empty ())
            return;
          moppe_ws_send (
            m_handle, bytes.data (), static_cast<int> (bytes.size ()));
        }

        void receive (std::vector<ReceivedDatagram>& out,
                      std::size_t limit) override {
          std::uint8_t scratch[max_ws_frame_bytes];
          for (std::size_t taken = 0; taken < limit; ++taken) {
            const int length = moppe_ws_recv (
              m_handle, scratch, static_cast<int> (sizeof scratch));
            if (length < 0)
              break;
            if (length == 0)
              continue;
            ReceivedDatagram packet;
            packet.from = { "relay" };
            packet.bytes.assign (scratch, scratch + length);
            out.push_back (std::move (packet));
          }
        }

        void flush () override {
          moppe_ws_flush (m_handle);
        }

        bool shared_channel () const override {
          return true;
        }

        bool ready () const override {
          return moppe_ws_ready (m_handle) != 0;
        }

        std::string description () const override {
          return m_url;
        }

        std::string last_error () const override {
          char message[256] {};
          const int written = moppe_ws_error (
            m_handle, message, static_cast<int> (sizeof message));
          return written > 0 ? std::string (message) : std::string ();
        }

      private:
        std::string m_url;
        int m_handle = 0;
      };
#endif

      class MemoryTransport final : public PeerTransport {
      public:
        MemoryTransport (MemoryHub& hub, std::string name)
            : m_hub (hub), m_name (std::move (name)) {}

        ~MemoryTransport () override {
          m_hub.leave (m_name);
        }

        void send_to (const PeerEndpoint& to,
                      std::span<const std::uint8_t> bytes) override {
          m_hub.enqueue (m_name, to.key, bytes);
        }

        void broadcast (std::span<const std::uint8_t> bytes) override {
          m_hub.enqueue_broadcast (m_name, bytes);
        }

        void receive (std::vector<ReceivedDatagram>& out,
                      std::size_t limit) override {
          m_hub.drain (m_name, out, limit);
        }

        void flush () override {
          m_hub.drop_for (m_name);
        }

        bool shared_channel () const override {
          return m_hub.shared_channel ();
        }

        std::string description () const override {
          return "memory:" + m_name;
        }

      private:
        MemoryHub& m_hub;
        std::string m_name;
      };
    }
  }

  bool is_relay_url (std::string_view target) {
    return target.starts_with ("ws://") || target.starts_with ("wss://");
  }

  std::string expand_multiplayer_target (std::string_view target) {
    const std::string clean = net_transport::trim_copy (target);
    if (!clean.starts_with ("room:"))
      return clean;
    const std::string room = net_transport::trim_copy (clean.substr (5));
    if (room.empty ())
      return {};
    std::string base = "ws://127.0.0.1:27960";
    if (const char* env = moppe::environment ("MOPPE_RELAY_URL"); env && *env)
      base = net_transport::trim_copy (env);
    while (!base.empty () && base.back () == '/')
      base.pop_back ();
    return base + "/room/" + room;
  }

  std::optional<RelayUrl> parse_relay_url (std::string_view raw,
                                           std::string& error) {
    const std::string text = net_transport::trim_copy (raw);
    RelayUrl url;
    std::size_t offset = 0;
    if (text.starts_with ("ws://")) {
      url.secure = false;
      url.port = 80;
      offset = 5;
    } else if (text.starts_with ("wss://")) {
      url.secure = true;
      url.port = 443;
      offset = 6;
    } else {
      error = "relay address must start with ws:// or wss://";
      return std::nullopt;
    }
    const std::size_t slash = text.find ('/', offset);
    const std::string authority = slash == std::string::npos
                                    ? text.substr (offset)
                                    : text.substr (offset, slash - offset);
    url.path = slash == std::string::npos ? "/default" : text.substr (slash);
    if (url.path == "/")
      url.path = "/default";
    if (authority.empty ()) {
      error = "relay address has no host";
      return std::nullopt;
    }
    const std::size_t colon = authority.rfind (':');
    if (colon != std::string::npos && authority.find (':') == colon) {
      url.host = authority.substr (0, colon);
      const std::string port_text = authority.substr (colon + 1);
      if (port_text.empty ()) {
        error = "relay address has empty port";
        return std::nullopt;
      }
      unsigned long parsed = 0;
      for (const char c : port_text) {
        if (!std::isdigit (static_cast<unsigned char> (c))) {
          error = "relay address has invalid port";
          return std::nullopt;
        }
        parsed = parsed * 10 + static_cast<unsigned long> (c - '0');
        if (parsed > 65535) {
          error = "relay port out of range";
          return std::nullopt;
        }
      }
      if (parsed == 0) {
        error = "relay port out of range";
        return std::nullopt;
      }
      url.port = static_cast<std::uint16_t> (parsed);
    } else {
      url.host = authority;
    }
    if (url.host.empty ()) {
      error = "relay address has no host";
      return std::nullopt;
    }
    url.full = text;
    return url;
  }

  bool parse_ipv4_endpoint (const PeerEndpoint& endpoint,
                            std::uint32_t& ip_host_order,
                            std::uint16_t& port) {
    unsigned a = 0, b = 0, c = 0, d = 0, p = 0;
    char trailing = 0;
    if (std::sscanf (endpoint.key.c_str (),
                     "%u.%u.%u.%u:%u%c",
                     &a,
                     &b,
                     &c,
                     &d,
                     &p,
                     &trailing) != 5)
      return false;
    if (a > 255 || b > 255 || c > 255 || d > 255 || p == 0 || p > 65535)
      return false;
    ip_host_order = (a << 24) | (b << 16) | (c << 8) | d;
    port = static_cast<std::uint16_t> (p);
    return true;
  }

  PeerEndpoint format_ipv4_endpoint (std::uint32_t ip_host_order,
                                     std::uint16_t port) {
    char text[32];
    std::snprintf (text,
                   sizeof text,
                   "%u.%u.%u.%u:%u",
                   (ip_host_order >> 24) & 0xFFu,
                   (ip_host_order >> 16) & 0xFFu,
                   (ip_host_order >> 8) & 0xFFu,
                   ip_host_order & 0xFFu,
                   static_cast<unsigned> (port));
    return { text };
  }

  std::unique_ptr<PeerTransport> open_udp_transport (const std::string& address,
                                                     std::uint16_t port,
                                                     std::string& error) {
#if MOPPE_NET_POSIX || MOPPE_NET_WIN32
    using namespace net_transport;
    if (!ensure_winsock (error))
      return nullptr;
    std::optional<sockaddr_in> named;
    if (!address.empty ()) {
      sockaddr_in resolved {};
      if (!resolve_ipv4 (address, port, resolved, error))
        return nullptr;
      named = resolved;
    }
    const SocketHandle fd = ::socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == invalid_socket) {
      error = "no UDP socket: " + last_socket_error ();
      return nullptr;
    }
    const int yes = 1;
    ::setsockopt (fd,
                  SOL_SOCKET,
                  SO_BROADCAST,
                  reinterpret_cast<const char*> (&yes),
                  sizeof yes);
    const int rcvbuf = 262144;
    ::setsockopt (fd,
                  SOL_SOCKET,
                  SO_RCVBUF,
                  reinterpret_cast<const char*> (&rcvbuf),
                  sizeof rcvbuf);
    set_nonblocking (fd);

    sockaddr_in here = ipv4_sockaddr (INADDR_ANY, port);
    if (::bind (fd, reinterpret_cast<sockaddr*> (&here), sizeof here) != 0) {
      // Another game on this machine has the port; any free one serves.
      here.sin_port = 0;
      if (::bind (fd, reinterpret_cast<sockaddr*> (&here), sizeof here) != 0) {
        error = "cannot listen: " + last_socket_error ();
        close_socket (fd);
        return nullptr;
      }
    }
    socklen_t size = sizeof here;
    ::getsockname (fd, reinterpret_cast<sockaddr*> (&here), &size);
    return std::make_unique<UdpTransport> (
      fd, ntohs (here.sin_port), port, named);
#else
    (void)address;
    (void)port;
    error = "direct UDP is unavailable here; join a ws:// or wss:// relay room";
    return nullptr;
#endif
  }

#if !defined(__APPLE__)
  std::unique_ptr<PeerTransport> open_apple_wss_transport (const std::string&,
                                                           std::string& error) {
    error =
      "wss:// relay requires Apple or browser TLS; use ws:// on this host";
    return nullptr;
  }
#endif

  std::unique_ptr<PeerTransport> open_relay_transport (const std::string& url,
                                                       std::string& error) {
    const std::optional<RelayUrl> parsed = parse_relay_url (url, error);
    if (!parsed)
      return nullptr;
#if MOPPE_NET_WEB
    return std::make_unique<net_transport::WebRelayTransport> (parsed->full);
#else
    if (parsed->secure)
      return open_apple_wss_transport (parsed->full, error);
#if MOPPE_NET_POSIX || MOPPE_NET_WIN32
    using namespace net_transport;
    if (!ensure_winsock (error))
      return nullptr;
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string port_str = std::to_string (parsed->port);
    if (::getaddrinfo (
          parsed->host.c_str (), port_str.c_str (), &hints, &found) != 0 ||
        !found) {
      error = "cannot resolve relay " + parsed->host;
      return nullptr;
    }
    SocketHandle fd = invalid_socket;
    for (addrinfo* each = found; each; each = each->ai_next) {
      fd = ::socket (each->ai_family, each->ai_socktype, each->ai_protocol);
      if (fd == invalid_socket)
        continue;
      const int nodelay = 1;
      ::setsockopt (fd,
                    IPPROTO_TCP,
                    TCP_NODELAY,
                    reinterpret_cast<const char*> (&nodelay),
                    sizeof nodelay);
      set_nonblocking (fd);
      const int status =
        ::connect (fd, each->ai_addr, static_cast<int> (each->ai_addrlen));
      if (status == 0 || last_would_block ())
        break;
      close_socket (fd);
      fd = invalid_socket;
    }
    ::freeaddrinfo (found);
    if (fd == invalid_socket) {
      error = "cannot connect to relay " + parsed->host + ":" + port_str;
      return nullptr;
    }
    return std::make_unique<TcpWebSocketTransport> (fd, *parsed);
#else
    error = "this platform has no WebSocket relay client";
    return nullptr;
#endif
#endif
  }

  MemoryHub::MemoryHub (Profile profile)
      : m_profile (profile), m_rng (profile.seed) {}

  void MemoryHub::set_profile (Profile profile) {
    m_profile = profile;
  }

  std::unique_ptr<PeerTransport> MemoryHub::join (std::string name) {
    m_members.push_back (name);
    return std::make_unique<net_transport::MemoryTransport> (*this,
                                                             std::move (name));
  }

  void MemoryHub::schedule (const std::string& from,
                            const std::string& to,
                            std::span<const std::uint8_t> bytes) {
    if (from == to)
      return;
    std::uniform_real_distribution<double> unit (0.0, 1.0);
    if (m_profile.loss > 0.0f * proportion[one] &&
        unit (m_rng) < m_profile.loss.numerical_value_in (one))
      return;
    const duration_t extra = m_profile.jitter > 0.0 * si::second
                               ? (unit (m_rng) - 0.5) * 2.0 * m_profile.jitter
                               : 0.0 * si::second;
    const duration_t delay =
      std::max (0.0 * si::second, m_profile.latency + extra);
    m_in_flight.push_back ({ .from = from,
                             .to = to,
                             .deliver_at = m_now + delay,
                             .order = m_next_order++,
                             .bytes = { bytes.begin (), bytes.end () } });
  }

  void MemoryHub::enqueue (const std::string& from,
                           const std::string& to,
                           std::span<const std::uint8_t> bytes) {
    if (std::ranges::find (m_members, to) == m_members.end ())
      return;
    schedule (from, to, bytes);
  }

  void MemoryHub::enqueue_broadcast (const std::string& from,
                                     std::span<const std::uint8_t> bytes) {
    for (const std::string& member : m_members)
      if (member != from)
        schedule (from, member, bytes);
  }

  void MemoryHub::drain (const std::string& who,
                         std::vector<ReceivedDatagram>& out,
                         std::size_t limit) {
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < m_in_flight.size (); ++i)
      if (m_in_flight[i].to == who && m_in_flight[i].deliver_at <= m_now)
        ready.push_back (i);
    std::ranges::sort (ready, [&] (std::size_t a, std::size_t b) {
      if (m_in_flight[a].deliver_at != m_in_flight[b].deliver_at)
        return m_in_flight[a].deliver_at < m_in_flight[b].deliver_at;
      return m_in_flight[a].order < m_in_flight[b].order;
    });
    if (ready.size () > limit)
      ready.resize (limit);
    for (const std::size_t idx : ready)
      out.push_back ({ .from = { m_in_flight[idx].from },
                       .bytes = std::move (m_in_flight[idx].bytes) });
    std::ranges::sort (ready, std::greater<> {});
    for (const std::size_t idx : ready)
      m_in_flight.erase (m_in_flight.begin () +
                         static_cast<std::ptrdiff_t> (idx));
  }

  void MemoryHub::drop_for (const std::string& who) {
    std::erase_if (m_in_flight,
                   [&] (const InFlight& item) { return item.to == who; });
  }

  void MemoryHub::leave (const std::string& who) {
    std::erase (m_members, who);
    drop_for (who);
  }
}
