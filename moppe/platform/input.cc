#include <moppe/environment.hh>
#include <moppe/platform/input.hh>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace moppe::platform {
  namespace {
    constexpr Key gamepad_keys[] = { Key::E,   Key::Restart, Key::Mount,
                                     Key::Tab, Key::Left,    Key::Right,
                                     Key::Up,  Key::Down,    Key::Space };
  }

  float stick_axis (float value) {
    constexpr float dead_zone = 0.12f;
    const float magnitude = std::abs (value);
    if (magnitude <= dead_zone)
      return 0;
    return std::copysign (
      std::min (1.0f, (magnitude - dead_zone) / (1.0f - dead_zone)), value);
  }

  ControlState GamepadMapper::map (const GamepadReading& r) {
    const float dpad_x = (r.dpad_right ? 1.0f : 0) - (r.dpad_left ? 1.0f : 0);
    const float dpad_y = (r.dpad_up ? 1.0f : 0) - (r.dpad_down ? 1.0f : 0);
    const float stick_x = stick_axis (r.left_x);
    const float stick_y = stick_axis (r.left_y);
    ControlState controls;
    controls.steer = stick_x != 0 ? stick_x : dpad_x;
    controls.drive = stick_y != 0 ? stick_y : dpad_y;
    controls.boost = std::max (std::clamp (r.right_trigger, 0.0f, 1.0f),
                               r.y ? 1.0f : 0.0f);
    edge (0, r.a, Key::E);
    edge (1, r.a, Key::Restart);
    edge (2, r.b, Key::Mount);
    edge (3, r.x, Key::Tab);
    edge (4, r.dpad_left, Key::Left);
    edge (5, r.dpad_right, Key::Right);
    edge (6, r.dpad_up, Key::Up);
    edge (7, r.dpad_down, Key::Down);
    edge (8, r.y, Key::Space);
    return controls;
  }

  void GamepadMapper::release () {
    for (int i = 0; i < 9; ++i)
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
      { "H", Key::H },           { "M", Key::M },
      { "N", Key::N },           { "R", Key::R },
      { "T", Key::T },           { "Y", Key::Y },
      { "1", Key::One },         { "2", Key::Two },
      { "3", Key::Three },       { "4", Key::Four },
      { "5", Key::Five },        { "6", Key::Six },
      { "7", Key::Seven },       { "Shift", Key::Shift },
      { "P", Key::Screenshot },
    };
    const auto found = keys.find (name);
    return found == keys.end () ? Key::Unknown : found->second;
  }

  void RemoteControl::poll (double now) {
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

  void RemoteControl::read (double now) {
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
    std::cerr << "moppe: control " << sequence << std::endl;
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

  void RemoteControl::schedule (const std::string& verb,
                                std::istringstream& words,
                                const std::string& line, double& at) {
    auto later = [&] (double when, std::function<void ()> action) {
      m_timeline.emplace (when, [line, action] {
        std::cerr << "moppe: remote " << line << std::endl;
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
      ControlState c;
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
      std::cerr << "moppe: remote cannot " << line << std::endl;
    }
  }
}
