#include <moppe/game/companion_transport.hh>

#import <Foundation/Foundation.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace moppe::game {
  namespace {
    struct AppleWssState {
      std::mutex mutex;
      std::deque<std::vector<std::uint8_t>> inbox;
      std::string error;
      std::atomic<bool> ready { false };
      std::atomic<bool> closed { false };
    };

    void schedule_receive (NSURLSessionWebSocketTask* task,
                           std::shared_ptr<AppleWssState> state) {
      [task receiveMessageWithCompletionHandler:^(
              NSURLSessionWebSocketMessage* message, NSError* err) {
        if (state->closed.load ())
          return;
        if (err) {
          std::lock_guard<std::mutex> lock (state->mutex);
          if (state->error.empty ())
            state->error = err.localizedDescription
                             ? err.localizedDescription.UTF8String
                             : "relay connection lost";
          state->ready.store (false);
          state->closed.store (true);
          return;
        }
        state->ready.store (true);
        if (message && message.type == NSURLSessionWebSocketMessageTypeData &&
            message.data && message.data.length > 0 &&
            message.data.length <= 16384) {
          const auto* bytes =
            static_cast<const std::uint8_t*> (message.data.bytes);
          std::vector<std::uint8_t> packet (bytes, bytes + message.data.length);
          std::lock_guard<std::mutex> lock (state->mutex);
          if (state->inbox.size () >= 128)
            state->inbox.pop_front ();
          state->inbox.push_back (std::move (packet));
        }
        schedule_receive (task, state);
      }];
    }

    class AppleWssTransport final : public PeerTransport {
    public:
      AppleWssTransport (NSURLSession* session,
                         NSURLSessionWebSocketTask* task,
                         std::shared_ptr<AppleWssState> state,
                         std::string url)
          : m_session (session), m_task (task), m_state (std::move (state)),
            m_url (std::move (url)) {
        [m_task resume];
        auto weak_state = m_state;
        [m_task sendPingWithPongReceiveHandler:^(NSError* err) {
          if (weak_state->closed.load ())
            return;
          if (!err) {
            weak_state->ready.store (true);
          } else {
            std::lock_guard<std::mutex> lock (weak_state->mutex);
            if (weak_state->error.empty ())
              weak_state->error = err.localizedDescription
                                    ? err.localizedDescription.UTF8String
                                    : "relay handshake failed";
            weak_state->closed.store (true);
          }
        }];
        schedule_receive (m_task, m_state);
      }

      ~AppleWssTransport () override {
        m_state->closed.store (true);
        [m_task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure
                             reason:nil];
        [m_session invalidateAndCancel];
      }

      void send_to (const PeerEndpoint&,
                    std::span<const std::uint8_t> bytes) override {
        broadcast (bytes);
      }

      void broadcast (std::span<const std::uint8_t> bytes) override {
        if (m_state->closed.load () || bytes.empty ())
          return;
        NSData* data = [NSData dataWithBytes:bytes.data ()
                                      length:bytes.size ()];
        auto* message =
          [[NSURLSessionWebSocketMessage alloc] initWithData:data];
        auto state = m_state;
        [m_task sendMessage:message
          completionHandler:^(NSError* err) {
            if (!err) {
              state->ready.store (true);
              return;
            }
            if (state->closed.load ())
              return;
            std::lock_guard<std::mutex> lock (state->mutex);
            if (state->error.empty ())
              state->error = err.localizedDescription
                               ? err.localizedDescription.UTF8String
                               : "relay send failed";
            state->ready.store (false);
            state->closed.store (true);
          }];
      }

      void receive (std::vector<ReceivedDatagram>& out,
                    std::size_t limit) override {
        std::lock_guard<std::mutex> lock (m_state->mutex);
        std::size_t taken = 0;
        while (!m_state->inbox.empty () && taken < limit) {
          out.push_back ({ .from = { "relay" },
                           .bytes = std::move (m_state->inbox.front ()) });
          m_state->inbox.pop_front ();
          ++taken;
        }
      }

      void flush () override {
        std::lock_guard<std::mutex> lock (m_state->mutex);
        m_state->inbox.clear ();
      }

      bool shared_channel () const override {
        return true;
      }

      bool ready () const override {
        return m_state->ready.load ();
      }

      std::string description () const override {
        return m_url;
      }

      std::string last_error () const override {
        std::lock_guard<std::mutex> lock (m_state->mutex);
        return m_state->error;
      }

    private:
      NSURLSession* m_session = nil;
      NSURLSessionWebSocketTask* m_task = nil;
      std::shared_ptr<AppleWssState> m_state;
      std::string m_url;
    };
  }

  std::unique_ptr<PeerTransport>
  open_apple_wss_transport (const std::string& url, std::string& error) {
    NSString* raw = [NSString stringWithUTF8String:url.c_str ()];
    NSURL* nsurl = raw ? [NSURL URLWithString:raw] : nil;
    if (!nsurl) {
      error = "invalid relay URL: " + url;
      return nullptr;
    }
    NSURLSessionConfiguration* config =
      [NSURLSessionConfiguration ephemeralSessionConfiguration];
    NSURLSession* session = [NSURLSession sessionWithConfiguration:config];
    NSURLSessionWebSocketTask* task = [session webSocketTaskWithURL:nsurl];
    auto state = std::make_shared<AppleWssState> ();
    return std::make_unique<AppleWssTransport> (
      session, task, std::move (state), url);
  }
}
