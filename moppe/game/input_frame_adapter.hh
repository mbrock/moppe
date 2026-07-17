#ifndef MOPPE_GAME_INPUT_FRAME_ADAPTER_HH
#define MOPPE_GAME_INPUT_FRAME_ADAPTER_HH

#include <moppe/game/input_frame.hh>
#include <moppe/platform/platform.hh>

#include <algorithm>

namespace moppe::game {
  // A narrow adapter for the callbacks already supplied by platform::Game.
  // It retains held controls until the next tick and emits one-shot actions
  // exactly once.  It is not an input-device framework.
  class InputFrameAdapter {
  public:
    void controls (const platform::ControlState& state) {
      m_held.turn = std::clamp (state.steer, -1.0f, 1.0f);
      m_held.drive = std::clamp (state.drive, -1.0f, 1.0f);
      m_held.boost = std::clamp (state.boost, 0.0f, 1.0f);
    }

    void key (platform::Key key, bool down) {
      using platform::Key;
      const float value = down ? 1.0f : 0.0f;

      // E is an edge-triggered gameplay action and historically skipped the
      // mount-combo state machine.
      if (key == Key::E) {
        if (down)
          m_deploy_glider = true;
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
        m_held.turn = -value;
        break;
      case Key::Right:
      case Key::D:
        m_held.turn = value;
        break;
      case Key::Up:
      case Key::W:
        m_held.drive = value;
        break;
      case Key::Down:
      case Key::S:
        m_held.drive = -value;
        break;
      case Key::Space:
        m_held.boost = value;
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
        m_held.turn = -value;
        break;
      case Key::Right:
      case Key::D:
        m_held.turn = value;
        break;
      case Key::Up:
      case Key::W:
        m_held.drive = value;
        break;
      case Key::Down:
      case Key::S:
        m_held.drive = -value;
        break;
      case Key::E:
        m_held.boost = value;
        break;
      default:
        break;
      }
    }

    InputFrame take_frame () {
      InputFrame frame = m_held;
      frame.deploy_glider = m_deploy_glider;
      frame.toggle_mount = m_toggle_mount;
      frame.cycle_camera = m_cycle_camera;
      frame.leave_cinematic = m_leave_cinematic;
      m_deploy_glider = false;
      m_toggle_mount = false;
      m_cycle_camera = false;
      m_leave_cinematic = false;
      return frame;
    }

    void clear () {
      m_held = {};
      m_deploy_glider = false;
      m_toggle_mount = false;
      m_cycle_camera = false;
      m_leave_cinematic = false;
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

    InputFrame m_held;
    bool m_deploy_glider = false;
    bool m_toggle_mount = false;
    bool m_cycle_camera = false;
    bool m_leave_cinematic = false;
    int m_combo = 0;
  };
}

#endif
