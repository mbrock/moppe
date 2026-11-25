#ifndef MOPPE_PLATFORM_HH
#define MOPPE_PLATFORM_HH

#include <moppe/render/renderer.hh>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace moppe {
  namespace platform {
    // Pointer coordinates, a drag, and the safe insets are lengths on the
    // screen in display points (y down): points_t, the HUD's own unit
    // (moppe/gfx/math.hh).
    using wheel_travel_t =
      mp_units::quantity<wheel_travel[mp_units::one], float>;

    // The host's monotonic clock: seconds since some moment before the
    // game started, never the wall's time of day.
    inline constexpr struct host_clock_epoch final
        : mp_units::absolute_point_origin<mp_units::isq::time> {
    } host_clock_epoch;
    using host_time_t =
      mp_units::quantity_point<mp_units::si::second, host_clock_epoch, double>;

    // Unified key codes: removes the old ASCII/GLUT_KEY_* numeric
    // collision ('d' == GLUT_KEY_LEFT == 100).  Only keys the game
    // actually uses.
    enum class Key {
      Unknown,
      Left,
      Right,
      Up,
      Down,
      W,
      A,
      S,
      D,
      Space,
      Tab,
      Escape,
      Mount,
      Restart,
      Screenshot,
      Record,
      E,
      G,
      H,
      J,
      L,
      M,
      N,
      R,
      T,
      Y,
      Q,
      One,
      Two,
      Three,
      Four,
      Five,
      Six,
      Seven,
      Shift,
      Enter,
      Backspace,
      // The key under Esc (`): the cheat menu.
      Cheat
    };

    struct Config {
      std::string title;
      int width = 1280; // ignored when fullscreen
      int height = 800;
      bool fullscreen = false;
      // An automated run: screenshots, captures, and benchmarks.
      bool capture_frames = false;
      bool activate = true; // let automated runs stay behind the active app
    };

    // Continuous controls used by touch/gamepad-style platforms.  The
    // driving axes are signed (-1..1); boost is a trigger (0..1).
    struct ControlState {
      control_signal_t steer {};
      control_signal_t drive {};
      proportion_t boost {};
      // How fast to turn the view, -1..1 rightward and upward: a stick
      // held over, as the mouse's pointer_move turns it by distance.
      control_signal_t look_x {};
      control_signal_t look_y {};
    };

    enum class PointerButton { Primary, Secondary, Middle };

    // Implemented by the game; driven by the per-OS run loop.
    //
    // Lifecycle: setup() must return quickly -- long world generation
    // belongs on a background thread (platform::async), with render()
    // drawing a loading screen until it finishes.  tick() receives
    // real dt clamped to 0.05 s.  key() edges are autorepeat-filtered;
    // releases are synthesized on focus loss / touch cancellation so
    // held inputs can't stick.  Pointer coordinates and deltas are in
    // y-down view points, matching HUD coordinates.
    class Game {
    public:
      virtual ~Game () {}
      virtual void setup (render::Renderer& r, int w_pts, int h_pts) = 0;
      virtual void resize (int w_pts, int h_pts) {
        (void)w_pts;
        (void)h_pts;
      }
      virtual void tick (seconds_t interval) = 0;
      virtual void render (render::Renderer& r) = 0;
      virtual bool loading () const {
        return false;
      }
      virtual void key (Key k, bool down) {
        (void)k;
        (void)down;
      }
      virtual void text_input (const char* utf8) {
        (void)utf8;
      }
      virtual void controls (const ControlState& state) {
        (void)state;
      }
      virtual void
      pointer_move (points_t x, points_t y, points_t dx, points_t dy) {
        (void)x;
        (void)y;
        (void)dx;
        (void)dy;
      }
      virtual void
      pointer_button (PointerButton button, bool down, points_t x, points_t y) {
        (void)button;
        (void)down;
        (void)x;
        (void)y;
      }
      virtual void
      pointer_scroll (points_t x, points_t y, wheel_travel_t delta) {
        (void)x;
        (void)y;
        (void)delta;
      }
      // The moment a remote-control timeline (input.hh) should keep, for
      // a game that is not stepping by the wall's clock -- one recording
      // itself frame by frame; before the epoch until it wants the
      // timeline to start. Absent, the host keeps the wall's time.
      virtual std::optional<host_time_t> script_seconds () const {
        return std::nullopt;
      }
    };

    // Runs the platform main loop; returns the process exit code. A
    // browser's loop is the page's: there run returns once the game is set
    // up, and the game lives on after main.
    int run (Game& game, const Config& config);

    // Ask the run loop to quit (no-op on iOS, where apps don't exit).
    void request_quit ();

    // Updates desktop window chrome without touching the rendered drawable.
    // No-op on platforms without a titled window.
    void set_window_title (const std::string& title);

    // Hides the pointer and keeps it from leaving the window, so pointer
    // movement turns a free camera without a button held. No-op on
    // platforms without a pointer.
    void set_pointer_captured (bool captured);

    // Turns UTF-8 text entry on or off (for typing a multiplayer address or
    // room). No-op on platforms without a text-input host.
    void set_text_input (bool active);

    // Resolve an asset-relative path ("textures/grass3.tga") to an
    // absolute path: the app bundle on Apple platforms, or the
    // executable/source directory for loose development builds.
    std::string asset_path (const std::string& relative);

    // Stable for one linked executable and different when any byte of that
    // executable changes. Cache paths live in the platform's user cache root.
    std::string executable_build_id ();
    std::string cache_path (const std::string& relative);

    // Monotonic time; never wall-clock.
    host_time_t now ();

    // The host clock's moment `seconds` after its epoch, for hosts that
    // read the OS's clock.
    inline host_time_t host_time (double seconds) {
      return host_clock_epoch + seconds * mp_units::si::second;
    }

    // Screen areas covered by notches / home indicators, in points.
    // Zero on macOS.  The HUD and touch zones stay inside these.
    struct Insets {
      points_t left {}, top {}, right {}, bottom {};
    };
    Insets safe_insets ();

    // Speak a phrase, asynchronously and best-effort.
    void say (const std::string& phrase);

    // Run work on a background (user-initiated QoS) queue, then call done on
    // the main/render thread.  The platform retains context through done(),
    // so a caller can make its raw callback context shutdown-safe.
    void async (void (*work) (void*),
                void (*done) (void*),
                std::shared_ptr<void> context);
  }
}

#endif
