// The game on Xbox Series consoles in Developer Mode: main.cc's main,
// compiled for this target as moppe_main, builds the game; platform::run
// hosts it in a CoreApplication view whose CoreWindow the NHAL renderer
// draws into through Direct3D 12, with a 3840x2160 swapchain the console
// shows natively, and reads the first gamepad.
//
// Everything the game writes to std::cerr goes to LocalState/log.txt.
// LocalState/control.txt drives the game remotely (tools/xbox-control
// uploads it through Device Portal); see Remote below.
// environment.txt in the package, then in LocalState, holds NAME=VALUE
// lines for moppe::environment; MOPPE_ARGS there is the command line.

#include <moppe/environment.hh>
#include <moppe/nhal/d3d12/d3d12_device.hh>
#include <moppe/nhal/renderer/nhal_renderer.hh>
#include <moppe/platform/platform.hh>
#include <moppe/platform/uwp/uwp.hh>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unknwn.h>
#include <windows.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.UI.Core.h>

int moppe_main (int argc, char** argv);

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Gaming::Input;
using namespace winrt::Windows::UI::Core;

namespace {
  using moppe::platform::Game;
  using moppe::platform::Key;

  float axis (double value) {
    return std::abs (value) < 0.15 ? 0.0f : float (value);
  }

  Key key_named (const std::string& name) {
    static const std::map<std::string, Key> keys {
      { "Left", Key::Left },   { "Right", Key::Right },
      { "Up", Key::Up },       { "Down", Key::Down },
      { "W", Key::W },         { "A", Key::A },
      { "S", Key::S },         { "D", Key::D },
      { "Space", Key::Space }, { "Tab", Key::Tab },
      { "Escape", Key::Escape }, { "Mount", Key::Mount },
      { "F", Key::Mount },     { "Restart", Key::Restart },
      { "E", Key::E },         { "G", Key::G },
      { "H", Key::H },         { "M", Key::M },
      { "N", Key::N },         { "R", Key::R },
      { "T", Key::T },         { "Y", Key::Y },
      { "1", Key::One },       { "2", Key::Two },
      { "3", Key::Three },     { "4", Key::Four },
      { "5", Key::Five },      { "6", Key::Six },
      { "7", Key::Seven },     { "Shift", Key::Shift },
      { "P", Key::Screenshot },
    };
    const auto found = keys.find (name);
    return found == keys.end () ? Key::Unknown : found->second;
  }

  // The game driven from afar: LocalState/control.txt, rewritten whole by
  // tools/xbox-control, starts with "seq N" and is read once per new N.
  // Its commands form a timeline from the moment it arrives:
  //
  //   tap KEY             press and release (KEY as on a Mac keyboard)
  //   hold KEY SECONDS    press, release after SECONDS
  //   down KEY / up KEY   press or release
  //   stick STEER DRIVE BOOST   analog controls, held until changed;
  //                       "stick off" gives them back to the gamepad
  //   look DX DY          turn the view as a mouse would, in points
  //   wait SECONDS        later commands start that much later
  //   env NAME VALUE      sets a moppe::environment switch (VALUE "-"
  //                       clears it), e.g. renderer probes
  //
  // Each command is logged as it runs.
  class Remote {
  public:
    Remote (Game& game, std::string path)
      : m_game (game), m_path (std::move (path)) {}

    // The analog controls the remote holds, if it holds them.
    const moppe::platform::ControlState* controls () const {
      return m_stick ? &m_controls : nullptr;
    }

    void poll (double now) {
      if (now >= m_next_check) {
        m_next_check = now + 0.2;
        read (now);
      }
      while (!m_timeline.empty () && m_timeline.begin ()->first <= now) {
        auto event = m_timeline.begin ();
        event->second ();
        m_timeline.erase (event);
      }
    }

  private:
    void read (double now) {
      std::error_code error;
      const auto stamp = std::filesystem::last_write_time (m_path, error);
      if (error || stamp == m_stamp)
        return;
      m_stamp = stamp;
      std::ifstream input (m_path);
      std::string word;
      long long sequence = -1;
      if (!(input >> word) || word != "seq" || !(input >> sequence)
          || sequence == m_sequence)
        return;
      m_sequence = sequence;
      std::cerr << "moppe-xbox: control " << sequence << std::endl;
      double at = now;
      std::string line;
      std::getline (input, line);
      while (std::getline (input, line)) {
        std::istringstream words (line);
        std::string verb;
        if (!(words >> verb) || verb[0] == '#')
          continue;
        schedule (verb, words, line, at);
      }
    }

    void schedule (const std::string& verb, std::istringstream& words,
                   const std::string& line, double& at) {
      auto later = [&] (double when, std::function<void ()> action) {
        m_timeline.emplace (when, [line, action] {
          std::cerr << "moppe-xbox: remote " << line << std::endl;
          action ();
        });
      };
      std::string name;
      double seconds = 0;
      if (verb == "wait" && words >> seconds) {
        at += seconds;
      } else if ((verb == "tap" || verb == "down" || verb == "up"
                  || verb == "hold")
                 && words >> name && key_named (name) != Key::Unknown) {
        const Key key = key_named (name);
        if (verb == "hold" && !(words >> seconds))
          seconds = 0.5;
        if (verb != "up")
          later (at, [this, key] { m_game.key (key, true); });
        if (verb == "tap" || verb == "hold")
          m_timeline.emplace (at + (verb == "tap" ? 0.1 : seconds),
                              [this, key] { m_game.key (key, false); });
        else if (verb == "up")
          later (at, [this, key] { m_game.key (key, false); });
      } else if (verb == "stick") {
        moppe::platform::ControlState c;
        std::string first;
        words >> first;
        const bool off = first == "off";
        if (!off) {
          c.steer = std::stof (first);
          words >> c.drive >> c.boost;
        }
        later (at, [this, c, off] {
          m_stick = !off;
          m_controls = c;
          if (off)
            m_game.controls ({});
        });
      } else if (verb == "env" && words >> name) {
        std::string value;
        words >> value;
        later (at, [name, value] {
          moppe::set_environment (name.c_str (),
                                  value == "-" ? nullptr : value.c_str ());
        });
      } else if (verb == "look") {
        float dx = 0, dy = 0;
        words >> dx >> dy;
        later (at, [this, dx, dy] { m_game.pointer_move (0, 0, dx, dy); });
      } else {
        std::cerr << "moppe-xbox: remote cannot " << line << std::endl;
      }
    }

    Game& m_game;
    std::string m_path;
    std::filesystem::file_time_type m_stamp {};
    long long m_sequence = -1;
    double m_next_check = 0;
    std::multimap<double, std::function<void ()>> m_timeline;
    bool m_stick = false;
    moppe::platform::ControlState m_controls {};
  };

  // The gamepad as the Apple hosts read a controller: the left stick (or
  // the D-pad) drives and steers, the right trigger boosts; A deploys the
  // glider or restarts, B mounts, X cycles the camera, Y boosts or flares,
  // and the D-pad also presses the arrow keys for menus.
  class Pad {
  public:
    explicit Pad (Game& game) : m_game (game) {}

    // Presses and releases the buttons' keys, and returns the sticks and
    // triggers, which the host gives the game unless the remote holds them.
    moppe::platform::ControlState poll () {
      auto pads = Gamepad::Gamepads ();
      if (pads.Size () == 0) {
        release ();
        return {};
      }
      const GamepadReading r = pads.GetAt (0).GetCurrentReading ();
      auto held = [&] (GamepadButtons b) { return (r.Buttons & b) == b; };
      const float dpad_x = (held (GamepadButtons::DPadRight) ? 1.0f : 0.0f)
                           - (held (GamepadButtons::DPadLeft) ? 1.0f : 0.0f);
      const float dpad_y = (held (GamepadButtons::DPadUp) ? 1.0f : 0.0f)
                           - (held (GamepadButtons::DPadDown) ? 1.0f : 0.0f);
      const float stick_x = axis (r.LeftThumbstickX);
      const float stick_y = axis (r.LeftThumbstickY);
      moppe::platform::ControlState controls;
      controls.steer = stick_x != 0 ? stick_x : dpad_x;
      controls.drive = stick_y != 0 ? stick_y : dpad_y;
      controls.boost = std::max (float (r.RightTrigger),
                                 held (GamepadButtons::Y) ? 1.0f : 0.0f);
      edge (0, held (GamepadButtons::A), Key::E);
      edge (1, held (GamepadButtons::A), Key::Restart);
      edge (2, held (GamepadButtons::B), Key::Mount);
      edge (3, held (GamepadButtons::X), Key::Tab);
      edge (4, held (GamepadButtons::DPadLeft), Key::Left);
      edge (5, held (GamepadButtons::DPadRight), Key::Right);
      edge (6, held (GamepadButtons::DPadUp), Key::Up);
      edge (7, held (GamepadButtons::DPadDown), Key::Down);
      edge (8, held (GamepadButtons::Y), Key::Space);
      m_connected = true;
      return controls;
    }

    bool connected () const { return m_connected; }

  private:
    void edge (int index, bool down, Key key) {
      if (m_buttons[index] == down)
        return;
      m_buttons[index] = down;
      m_game.key (key, down);
    }

    // A controller that disappears lets go of everything it held.
    void release () {
      if (!m_connected)
        return;
      static constexpr Key keys[] = { Key::E,   Key::Restart, Key::Mount,
                                      Key::Tab, Key::Left,    Key::Right,
                                      Key::Up,  Key::Down,    Key::Space };
      for (int i = 0; i < 9; ++i)
        if (m_buttons[i]) {
          m_game.key (keys[i], false);
          m_buttons[i] = false;
        }
      m_game.controls ({});
      m_connected = false;
    }

    Game& m_game;
    bool m_buttons[9] {};
    bool m_connected = false;
  };

  struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    explicit App (Game& game) : m_game (game) {}

    IFrameworkView CreateView () { return *this; }

    void Initialize (CoreApplicationView const& view) {
      view.Activated ([this] (auto&&, auto&&) {
        CoreWindow::GetForCurrentThread ().Activate ();
        m_activated = true;
      });
    }

    void SetWindow (CoreWindow const& window) {
      m_window = window;
      window.Closed ([this] (auto&&, auto&&) { m_closed = true; });
    }

    void Load (hstring const&) {}
    void Uninitialize () {}

    void Run () {
      // Present nothing until the compositor shows the window.
      while (!m_activated && !m_closed)
        m_window.Dispatcher ().ProcessEvents (
          CoreProcessEventsOption::ProcessOneAndAllPending);
      try {
        play ();
      } catch (const std::exception& error) {
        std::cerr << "moppe-xbox: " << error.what () << std::endl;
      } catch (hresult_error const& error) {
        std::cerr << "moppe-xbox: " << to_string (error.message ())
                  << std::endl;
      }
    }

  private:
    void play () {
      using namespace moppe;
      auto device = nhal::create_d3d12_device (
        winrt::get_unknown (m_window), 3840, 2160, nhal::Format::bgra8_unorm);
      auto renderer = nhal::create_renderer (
        std::move (device), nhal::world_shaders_d3d12 (), 2.0f);
      m_game.setup (*renderer, renderer->width_pts (),
                    renderer->height_pts ());

      // Names the device's step in the log when frames stop advancing.
      std::atomic<long> frames { 0 };
      std::atomic<bool> stopping { false };
      std::thread watchdog ([&] {
        long seen = -1;
        while (!stopping) {
          std::this_thread::sleep_for (std::chrono::seconds (3));
          const long now = frames;
          if (now == seen)
            std::cerr << "moppe-xbox: stalled at frame " << now << ": "
                      << nhal::d3d12_device_step () << std::endl;
          seen = now;
        }
      });

      Pad pad (m_game);
      Remote remote (m_game,
                     platform::uwp::local_state_path () + "control.txt");
      const auto start = std::chrono::steady_clock::now ();
      auto last = std::chrono::steady_clock::now ();
      auto report_start = last;
      long report_frames = 0;
      double slowest = 0;
      while (!m_closed && !platform::uwp::quit_requested ()) {
        m_window.Dispatcher ().ProcessEvents (
          CoreProcessEventsOption::ProcessAllIfPresent);
        platform::uwp::run_main_thread_tasks ();
        const platform::ControlState held = pad.poll ();
        const auto now = std::chrono::steady_clock::now ();
        remote.poll (std::chrono::duration<double> (now - start).count ());
        if (remote.controls ())
          m_game.controls (*remote.controls ());
        else if (pad.connected ())
          m_game.controls (held);
        const double dt = std::chrono::duration<double> (now - last).count ();
        last = now;
        m_game.tick (float (std::clamp (dt, 0.0, 0.05)));
        m_game.render (*renderer);
        ++frames;
        // The frame rate every ten seconds, beside the renderer's own
        // pass timings (MOPPE_NHAL_TIMINGS).
        slowest = std::max (slowest, dt);
        const double span =
          std::chrono::duration<double> (now - report_start).count ();
        if (++report_frames > 1 && span >= 10.0) {
          std::cerr << "moppe-xbox: " << report_frames / span
                    << " fps, slowest frame " << slowest * 1000 << " ms"
                    << std::endl;
          report_start = now;
          report_frames = 0;
          slowest = 0;
        }
      }
      stopping = true;
      watchdog.join ();
    }

    Game& m_game;
    CoreWindow m_window { nullptr };
    bool m_closed = false;
    bool m_activated = false;
  };

  std::vector<std::string> split (const std::string& line) {
    std::vector<std::string> words;
    std::istringstream input (line);
    for (std::string word; input >> word;)
      words.push_back (word);
    return words;
  }
}

namespace moppe::platform {
  int run (Game& game, const Config&) {
    CoreApplication::Run (make<App> (game));
    return 0;
  }
}

int __stdcall wWinMain (HINSTANCE, HINSTANCE, PWSTR, int) {
  init_apartment ();
  namespace uwp = moppe::platform::uwp;
  const std::string local = uwp::local_state_path ();
  static std::ofstream log (local + "log.txt", std::ios::trunc);
  if (log)
    std::cerr.rdbuf (log.rdbuf ());
  const int packaged = uwp::load_environment_file (
    moppe::platform::asset_path ("environment.txt"));
  const int local_variables =
    uwp::load_environment_file (local + "environment.txt");
  // A development console reports where its GPU time goes.
  if (!moppe::environment ("MOPPE_NHAL_TIMINGS"))
    moppe::set_environment ("MOPPE_NHAL_TIMINGS", "1");
  std::cerr << "moppe-xbox: " << packaged << " packaged and "
            << local_variables << " local environment variables" << std::endl;

  std::vector<std::string> words { "moppe" };
  if (const char* arguments = moppe::environment ("MOPPE_ARGS"))
    for (std::string& word : split (arguments))
      words.push_back (std::move (word));
  std::vector<char*> argv;
  for (std::string& word : words)
    argv.push_back (word.data ());
  argv.push_back (nullptr);
  const int status = moppe_main (int (words.size ()), argv.data ());
  std::cerr << "moppe-xbox: exit " << status << std::endl;
  return status;
}
