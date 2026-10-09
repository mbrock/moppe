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
      m_analog.turn = std::clamp (state.steer, -1.0f, 1.0f);
      m_analog.drive = std::clamp (state.drive, -1.0f, 1.0f);
      m_analog.boost = std::clamp (state.boost, 0.0f, 1.0f);
      m_look_rate_x = std::clamp (state.look_x, -1.0f, 1.0f);
      m_look_rate_y = std::clamp (state.look_y, -1.0f, 1.0f);
    }

    // Pointer movement in points becomes head turn in radians.
    void look (float dx, float dy) {
      m_look_yaw += 0.0035f * dx;
      m_look_pitch -= 0.0035f * dy;
    }

    void key (platform::Key key, bool down) {
      using platform::Key;
      const float value = down ? 1.0f : 0.0f;

      if (key == Key::Shift) {
        m_run = down;
        return;
      }

      // E has an activation edge plus held state so deployment can wait for
      // the bike to reach a safe height. It skips the mount-combo state
      // machine.
      if (key == Key::E) {
        if (down && !m_deploy_glider_held)
          m_deploy_glider = true;
        m_deploy_glider_held = down;
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
        m_keys.boost = value;
        break;
      default:
        break;
      }
    }

    void cinematic_key (platform::Key key, bool down) {
      using platform::Key;
      const float value = down ? 1.0f : 0.0f;
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
        m_keys.boost = value;
        break;
      default:
        break;
      }
    }

    // A held look stick turns the head at up to these rates, in radians
    // per second, over the `dt` the frame covers.
    static constexpr float look_yaw_rate = 3.2f;
    static constexpr float look_pitch_rate = 2.0f;

    InputFrame take_frame (float dt = 0.0f) {
      InputFrame frame = m_analog;
      m_look_yaw += look_yaw_rate * m_look_rate_x * dt;
      m_look_pitch += look_pitch_rate * m_look_rate_y * dt;
      if (std::abs (input_value (m_keys.turn)) >
          std::abs (input_value (frame.turn)))
        frame.turn = m_keys.turn;
      if (std::abs (input_value (m_keys.drive)) >
          std::abs (input_value (frame.drive)))
        frame.drive = m_keys.drive;
      frame.boost =
        std::max (input_value (frame.boost), input_value (m_keys.boost));
      frame.deploy_glider = m_deploy_glider;
      frame.deploy_glider_held = m_deploy_glider_held;
      frame.toggle_mount = m_toggle_mount;
      frame.cycle_camera = m_cycle_camera;
      frame.leave_cinematic = m_leave_cinematic;
      frame.look_yaw = m_look_yaw;
      frame.look_pitch = m_look_pitch;
      frame.run = m_run;
      m_look_yaw = 0.0f;
      m_look_pitch = 0.0f;
      m_deploy_glider = false;
      m_toggle_mount = false;
      m_cycle_camera = false;
      m_leave_cinematic = false;
      return frame;
    }

    void clear () {
      m_analog = {};
      m_keys = {};
      m_deploy_glider = false;
      m_deploy_glider_held = false;
      m_toggle_mount = false;
      m_cycle_camera = false;
      m_leave_cinematic = false;
      m_look_yaw = 0.0f;
      m_look_pitch = 0.0f;
      m_look_rate_x = 0.0f;
      m_look_rate_y = 0.0f;
      m_run = false;
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
    bool m_deploy_glider = false;
    bool m_deploy_glider_held = false;
    bool m_toggle_mount = false;
    bool m_cycle_camera = false;
    bool m_leave_cinematic = false;
    float m_look_yaw = 0.0f;
    float m_look_pitch = 0.0f;
    float m_look_rate_x = 0.0f;
    float m_look_rate_y = 0.0f;
    bool m_run = false;
    int m_combo = 0;
  };
}

#endif
