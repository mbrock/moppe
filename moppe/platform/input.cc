#include <moppe/environment.hh>
#include <moppe/platform/input.hh>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace moppe::platform {
  namespace {
    using mp_units::one;

    constexpr control_signal_t signal (float value) {
      return value * control_signal[one];
    }

    constexpr Key gamepad_keys[] = { Key::E,   Key::Restart, Key::Mount,
                                     Key::Tab, Key::Left,    Key::Right,
                                     Key::Up,  Key::Down,    Key::Space,
                                     Key::R,   Key::T,       Key::Shift };
  }

  control_signal_t stick_axis (control_signal_t value) {
    constexpr control_signal_t dead_zone = signal (0.12f);
    constexpr control_signal_t full = signal (1.0f);
    const control_signal_t magnitude = abs (value);
    if (magnitude <= dead_zone)
      return {};
    const proportion_t travel =
      std::min (1.0f,
                ((magnitude - dead_zone) / (full - dead_zone))
                  .numerical_value_in (one)) *
      proportion[one];
    return signal (std::copysign (travel.numerical_value_in (one),
                                  value.numerical_value_in (one)));
  }

  void look_axes (control_signal_t x,
                  control_signal_t y,
                  control_signal_t& look_x,
                  control_signal_t& look_y) {
    constexpr control_signal_t dead_zone = signal (0.15f);
    constexpr control_signal_t full = signal (1.0f);
    // The deflection's size, as the stick's own axes measure it.
    const control_signal_t magnitude =
      signal (std::sqrt ((x * x + y * y).numerical_value_in (one)));
    look_x = look_y = {};
    if (magnitude <= dead_zone)
      return;
    const float travel = std::min (
      1.0f,
      ((magnitude - dead_zone) / (full - dead_zone)).numerical_value_in (one));
    const control_signal_t rate =
      signal (travel * (0.25f + 0.75f * travel * travel));
    look_x = signal ((x / magnitude * rate).numerical_value_in (one));
    look_y = signal ((y / magnitude * rate).numerical_value_in (one));
  }

  ControlState GamepadMapper::map (const GamepadReading& r) {
    const control_signal_t dpad_x =
      signal ((r.dpad_right ? 1.0f : 0) - (r.dpad_left ? 1.0f : 0));
    const control_signal_t dpad_y =
      signal ((r.dpad_up ? 1.0f : 0) - (r.dpad_down ? 1.0f : 0));
    const control_signal_t stick_x = stick_axis (r.left_x);
    const control_signal_t stick_y = stick_axis (r.left_y);
    ControlState controls;
    controls.steer = stick_x != control_signal_t {} ? stick_x : dpad_x;
    controls.drive = stick_y != control_signal_t {} ? stick_y : dpad_y;
    constexpr proportion_t none = 0.0f * proportion[one];
    constexpr proportion_t all = 1.0f * proportion[one];
    controls.boost =
      std::max (std::clamp (r.right_trigger, none, all), r.y ? all : none);
    look_axes (r.right_x, r.right_y, controls.look_x, controls.look_y);
    edge (0, r.a, Key::E);
    edge (1, r.a, Key::Restart);
    edge (2, r.b, Key::Mount);
    edge (3, r.x, Key::Tab);
    edge (4, r.dpad_left, Key::Left);
    edge (5, r.dpad_right, Key::Right);
    edge (6, r.dpad_up, Key::Up);
    edge (7, r.dpad_down, Key::Down);
    edge (8, r.y, Key::Space);
    edge (9, r.left_shoulder, Key::R);
    edge (10, r.right_shoulder, Key::T);
    edge (11, r.left_trigger > 0.4f * proportion[one], Key::Shift);
    return controls;
  }

  void GamepadMapper::release () {
    for (int i = 0; i < 12; ++i)
      if (m_buttons[i]) {
        m_game.key (gamepad_keys[i], false);
        m_buttons[i] = false;
      }
    m_game.controls ({});
  }

  void GamepadMapper::edge (int index, bool down, Key key) {
    if (m_buttons[index] == down)
      return;
    m_buttons[index] = down;
    m_game.key (key, down);
  }

  Key key_named (const std::string& name) {
    static const std::map<std::string, Key> keys {
      { "Left", Key::Left },     { "Right", Key::Right },
      { "Up", Key::Up },         { "Down", Key::Down },
      { "W", Key::W },           { "A", Key::A },
      { "S", Key::S },           { "D", Key::D },
      { "Space", Key::Space },   { "Tab", Key::Tab },
      { "Escape", Key::Escape }, { "Mount", Key::Mount },
      { "F", Key::Mount },       { "Restart", Key::Restart },
      { "E", Key::E },           { "G", Key::G },
      { "H", Key::H },           { "L", Key::L },
      { "M", Key::M },           { "N", Key::N },
      { "R", Key::R },           { "T", Key::T },
      { "Y", Key::Y },           { "Q", Key::Q },
      { "1", Key::One },         { "2", Key::Two },
      { "3", Key::Three },       { "4", Key::Four },
      { "5", Key::Five },        { "6", Key::Six },
      { "7", Key::Seven },       { "Shift", Key::Shift },
      { "P", Key::Screenshot },  { "V", Key::Record },
    };
    const auto found = keys.find (name);
    return found == keys.end () ? Key::Unknown : found->second;
  }

  Key touch_key (int character) {
    switch (character) {
    case ' ':
      return Key::Space;
    case '\t':
      return Key::Tab;
    case '^':
      return Key::Shift;
    default:
      return key_named (std::string (1, char (character)));
    }
  }

  int touch_character (Key key) {
    switch (key) {
    case Key::Space:
      return ' ';
    case Key::Tab:
      return '\t';
    case Key::Shift:
      return '^';
    case Key::Mount:
      return 'F';
    case Key::E:
      return 'E';
    case Key::R:
      return 'R';
    case Key::T:
      return 'T';
    default:
      return 0;
    }
  }

  bool offer_actions (const std::vector<Offer>& offers) {
#ifdef __EMSCRIPTEN__
    // One line an offer, its key's character and its words; the page hears
    // of them only when they change.
    static std::string told = "\n";
    std::string lines;
    for (const Offer& offer : offers)
      if (const int character = touch_character (offer.key))
        lines += std::to_string (character) + ' ' + offer.action + '\n';
    if (lines != told) {
      told = lines;
      EM_ASM ({ Module['moppeOffers'](UTF8ToString ($0)); }, lines.c_str ());
    }
    return EM_ASM_INT ({ return Module['moppeTouching'] ? 1 : 0; }) != 0;
#else
    (void)offers;
    return false;
#endif
  }

  void RemoteControl::poll (host_time_t now) {
    if (now >= m_next_check) {
      m_next_check = now + 0.2 * mp_units::si::second;
      read (now);
    }
    while (!m_timeline.empty () && m_timeline.begin ()->first <= now) {
      auto event = m_timeline.begin ();
      event->second ();
      m_timeline.erase (event);
    }
  }

  void RemoteControl::read (host_time_t now) {
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time (m_path, error);
    if (error || stamp == m_stamp)
      return;
    m_stamp = stamp;
    std::ifstream input (m_path);
    std::string word;
    long long sequence = -1;
    if (!(input >> word) || word != "seq" || !(input >> sequence) ||
        sequence == m_sequence)
      return;
    m_sequence = sequence;
    std::cerr << "moppe: control " << sequence << std::endl;
    host_time_t at = now;
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

  void RemoteControl::schedule (const std::string& verb,
                                std::istringstream& words,
                                const std::string& line,
                                host_time_t& at) {
    using mp_units::si::second;
    auto later = [&] (host_time_t when, std::function<void ()> action) {
      m_timeline.emplace (when, [line, action] {
        std::cerr << "moppe: remote " << line << std::endl;
        action ();
      });
    };
    std::string name;
    // Read off the line as written, then a duration.
    double seconds = 0;
    if (verb == "wait" && words >> seconds) {
      at += seconds * second;
    } else if ((verb == "tap" || verb == "down" || verb == "up" ||
                verb == "hold") &&
               words >> name && key_named (name) != Key::Unknown) {
      const Key key = key_named (name);
      if (verb == "hold" && !(words >> seconds))
        seconds = 0.5;
      if (verb != "up")
        later (at, [this, key] { m_game.key (key, true); });
      if (verb == "tap" || verb == "hold")
        m_timeline.emplace (at + (verb == "tap" ? 0.1 : seconds) * second,
                            [this, key] { m_game.key (key, false); });
      else if (verb == "up")
        later (at, [this, key] { m_game.key (key, false); });
    } else if (verb == "stick") {
      ControlState c;
      std::string first;
      words >> first;
      const bool off = first == "off";
      if (!off) {
        float drive = 0, boost = 0;
        words >> drive >> boost;
        c.steer = signal (std::stof (first));
        c.drive = signal (drive);
        c.boost = boost * proportion[one];
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
      later (at, [this, dx, dy] {
        constexpr view_points_t origin {};
        m_game.pointer_move (origin, origin, dx * view_point, dy * view_point);
      });
    } else {
      std::cerr << "moppe: remote cannot " << line << std::endl;
    }
  }
}
