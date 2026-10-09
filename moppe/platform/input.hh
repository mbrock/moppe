#ifndef MOPPE_PLATFORM_INPUT_HH
#define MOPPE_PLATFORM_INPUT_HH

// Input every host shares: the gamepad's mapping onto the game, the keys
// by name, and the remote-control file that scripts and agents drive the
// game with. Hosts read their devices; these turn the readings into the
// Game's controls and key edges the same way everywhere.

#include <moppe/platform/platform.hh>

#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <string>

namespace moppe::platform {
  // One reading of a gamepad in the Xbox layout. Sticks run -1..1 with y up,
  // before any dead zone; the trigger runs 0..1.
  struct GamepadReading {
    float left_x = 0, left_y = 0;
    float right_x = 0, right_y = 0;
    float right_trigger = 0;
    bool a = false, b = false, x = false, y = false;
    bool dpad_left = false, dpad_right = false;
    bool dpad_up = false, dpad_down = false;
  };

  // A stick axis past its dead zone, rescaled so the edge of the dead zone
  // reads zero and full travel one.
  float stick_axis (float value);

  // The right stick's deflection as a look rate: a round dead zone, so a
  // push straight up does not leak sideways, and a curve that keeps small
  // deflections fine for aiming and full ones quick for turning around.
  void look_axes (float x, float y, float& look_x, float& look_y);

  // The gamepad as every host reads one: the left stick (or the D-pad)
  // drives and steers, the right stick looks around, the right trigger
  // boosts; A deploys the glider or
  // restarts, B mounts, X cycles the camera, Y boosts or flares, and the
  // D-pad also presses the arrow keys for menus.
  class GamepadMapper {
  public:
    explicit GamepadMapper (Game& game) : m_game (game) {}

    // Presses and releases the buttons' keys and returns the controls the
    // sticks and triggers ask for; the host gives them to the game.
    ControlState map (const GamepadReading& reading);
    // A controller that disappears lets go of everything it held.
    void release ();

  private:
    void edge (int index, bool down, Key key);

    Game& m_game;
    bool m_buttons[9] {};
  };

  // A Key by the name a Mac keyboard gives it ("Space", "F", "1"), or
  // Key::Unknown.
  Key key_named (const std::string& name);

  // The game driven from a file, rewritten whole by a script or agent
  // (tools/xbox-control on the console). It starts with "seq N" and is
  // read once per new N; its commands form a timeline from the moment it
  // arrives:
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
  class RemoteControl {
  public:
    RemoteControl (Game& game, std::string path)
      : m_game (game), m_path (std::move (path)) {}

    // The analog controls the remote holds, if it holds them.
    const ControlState* controls () const {
      return m_stick ? &m_controls : nullptr;
    }

    // Reads the file now and then and runs what is due at `now` seconds.
    void poll (double now);

  private:
    void read (double now);
    void schedule (const std::string& verb, std::istringstream& words,
                   const std::string& line, double& at);

    Game& m_game;
    std::string m_path;
    std::filesystem::file_time_type m_stamp {};
    long long m_sequence = -1;
    double m_next_check = 0;
    std::multimap<double, std::function<void ()>> m_timeline;
    bool m_stick = false;
    ControlState m_controls {};
  };
}

#endif
