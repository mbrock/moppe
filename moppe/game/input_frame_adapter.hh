#ifndef MOPPE_GAME_INPUT_FRAME_ADAPTER_HH
#define MOPPE_GAME_INPUT_FRAME_ADAPTER_HH

#include <moppe/game/input_frame.hh>
#include <moppe/platform/platform.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  // A narrow adapter for the callbacks already supplied by platform::Game.
  // It retains held controls until the next tick and emits one-shot actions
  // exactly once.  It is not an input-device framework.
  class InputFrameAdapter {
  public:
    void controls (const platform::ControlState& state) {
      constexpr control_signal_t back = -1.0f * control_signal[one];
      constexpr control_signal_t ahead = 1.0f * control_signal[one];
      constexpr proportion_t none = 0.0f * proportion[one];
      constexpr proportion_t all = 1.0f * proportion[one];
      m_analog.turn = std::clamp (state.steer, back, ahead);
      m_analog.drive = std::clamp (state.drive, back, ahead);
      m_analog.boost = std::clamp (state.boost, none, all);
      m_look_rate_x = std::clamp (state.look_x, back, ahead);
      m_look_rate_y = std::clamp (state.look_y, back, ahead);
    }

    // Pointer movement in points becomes head turn in radians.
    void look (points_t dx, points_t dy) {
      m_look_yaw += 0.0035f * dx.numerical_value_in (display_point) * u::rad;
      m_look_pitch -= 0.0035f * dy.numerical_value_in (display_point) * u::rad;
    }

    void key (platform::Key key, bool down) {
      using platform::Key;
      const control_signal_t value = (down ? 1.0f : 0.0f) * control_signal[one];
      const proportion_t pressed = (down ? 1.0f : 0.0f) * proportion[one];

      if (key == Key::Shift) {
        m_run = down;
        return;
      }

      // E has an activation edge plus held state so deployment can wait for
      // the bike to reach a safe height. It skips the mount-combo state
      // machine.
      if (key == Key::E) {
        if (down && !m_use_held)
          m_use = true;
        m_use_held = down;
        return;
      }
      if (key == Key::Mount) {
        if (down)
          m_toggle_mount = true;
        return;
      }
      if (key == Key::Tab) {
        if (down) {
          record_mount_combo (key);
          m_cycle_camera = true;
        }
        return;
      }

      const bool arrow = key == Key::Left || key == Key::Right ||
                         key == Key::Up || key == Key::Down;
      if (down && !arrow)
        record_mount_combo (key);

      switch (key) {
      case Key::Left:
      case Key::A:
        m_keys.turn = -value;
        break;
      case Key::Right:
      case Key::D:
        m_keys.turn = value;
        break;
      case Key::Up:
      case Key::W:
        m_keys.drive = value;
        break;
      case Key::Down:
      case Key::S:
        m_keys.drive = -value;
        break;
      case Key::Space:
        m_keys.boost = pressed;
        break;
      case Key::R:
        m_hang_hammock = m_hang_hammock || down;
        m_lean_back = down;
        break;
      case Key::T:
        m_light_fire = m_light_fire || down;
        break;
      default:
        break;
      }
    }

    void cinematic_key (platform::Key key, bool down) {
      using platform::Key;
      const control_signal_t value = (down ? 1.0f : 0.0f) * control_signal[one];
      const proportion_t pressed = (down ? 1.0f : 0.0f) * proportion[one];
      switch (key) {
      case Key::Space:
        if (down)
          m_leave_cinematic = true;
        break;
      case Key::Left:
      case Key::A:
        m_keys.turn = -value;
        break;
      case Key::Right:
      case Key::D:
        m_keys.turn = value;
        break;
      case Key::Up:
      case Key::W:
        m_keys.drive = value;
        break;
      case Key::Down:
      case Key::S:
        m_keys.drive = -value;
        break;
      case Key::E:
        m_keys.boost = pressed;
        break;
      default:
        break;
      }
    }

    // A held look stick turns the head at up to these rates over the
    // `dt` the frame covers.
    static constexpr radians_per_second_t look_yaw_rate = 3.2f * u::rad / u::s;
    static constexpr radians_per_second_t look_pitch_rate =
      2.0f * u::rad / u::s;

    InputFrame take_frame (seconds_t dt = seconds_t {}) {
      InputFrame frame = m_analog;
      m_look_yaw += look_yaw_rate * input_value (m_look_rate_x) * dt;
      m_look_pitch += look_pitch_rate * input_value (m_look_rate_y) * dt;
      if (std::abs (input_value (m_keys.turn)) >
          std::abs (input_value (frame.turn)))
        frame.turn = m_keys.turn;
      if (std::abs (input_value (m_keys.drive)) >
          std::abs (input_value (frame.drive)))
        frame.drive = m_keys.drive;
      frame.boost = std::max (frame.boost, m_keys.boost);
      frame.use = m_use;
      frame.use_held = m_use_held;
      frame.toggle_mount = m_toggle_mount;
      frame.cycle_camera = m_cycle_camera;
      frame.leave_cinematic = m_leave_cinematic;
      frame.hang_hammock = m_hang_hammock;
      frame.light_fire = m_light_fire;
      frame.lean_back = m_lean_back;
      frame.look_yaw = m_look_yaw;
      frame.look_pitch = m_look_pitch;
      frame.run = m_run;
      m_look_yaw = radians_t {};
      m_look_pitch = radians_t {};
      m_use = false;
      m_toggle_mount = false;
      m_cycle_camera = false;
      m_leave_cinematic = false;
      m_hang_hammock = false;
      m_light_fire = false;
      return frame;
    }

    void clear () {
      m_analog = {};
      m_keys = {};
      m_use = false;
      m_use_held = false;
      m_toggle_mount = false;
      m_cycle_camera = false;
      m_leave_cinematic = false;
      m_hang_hammock = false;
      m_light_fire = false;
      m_look_yaw = radians_t {};
      m_look_pitch = radians_t {};
      m_look_rate_x = {};
      m_look_rate_y = {};
      m_run = false;
      m_lean_back = false;
      m_combo = 0;
    }

  private:
    void record_mount_combo (platform::Key key) {
      using platform::Key;
      static constexpr Key wanted[] = { Key::Seven, Key::Five, Key::R };
      if (key == wanted[m_combo]) {
        if (++m_combo == 3) {
          m_combo = 0;
          m_toggle_mount = true;
        }
      } else {
        m_combo = key == Key::Seven ? 1 : 0;
      }
    }

    InputFrame m_analog;
    InputFrame m_keys;
    bool m_use = false;
    bool m_use_held = false;
    bool m_toggle_mount = false;
    bool m_cycle_camera = false;
    bool m_leave_cinematic = false;
    bool m_hang_hammock = false;
    bool m_light_fire = false;
    radians_t m_look_yaw {};
    radians_t m_look_pitch {};
    control_signal_t m_look_rate_x {};
    control_signal_t m_look_rate_y {};
    bool m_run = false;
    bool m_lean_back = false;
    int m_combo = 0;
  };
}

#endif
