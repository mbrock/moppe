#import <GameController/GameController.h>

#include <moppe/platform/apple/game_controller.hh>
#include <moppe/platform/platform.hh>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace moppe::platform {
  namespace {
    float axis_with_dead_zone (float value) {
      constexpr float dead_zone = 0.12f;
      const float magnitude = std::abs (value);
      if (magnitude <= dead_zone)
        return 0;
      const float scaled =
        std::min (1.0f, (magnitude - dead_zone) / (1.0f - dead_zone));
      return std::copysign (scaled, value);
    }

    bool button_down (GCControllerButtonInput* button) {
      return button && button.pressed;
    }
  }

  class AppleGameController::Impl {
  public:
    explicit Impl (Game& game) : m_game (game) {}

    void poll () {
      GCController* controller = choose_controller ();
      if (controller != m_controller) {
        release_all ();
        m_controller = controller;
        if (controller) {
          NSString* name = controller.vendorName ?: controller.productCategory;
          std::cerr << "moppe: game controller connected: "
                    << (name ? name.UTF8String : "unknown") << std::endl;
        } else if (m_had_controller) {
          std::cerr << "moppe: game controller disconnected" << std::endl;
        }
        m_had_controller = controller != nil;
      }

      if (!controller)
        return;

      ControlState controls;
      GCExtendedGamepad* extended = controller.extendedGamepad;
      if (extended) {
        const float stick_x =
          axis_with_dead_zone (extended.leftThumbstick.xAxis.value);
        const float stick_y =
          axis_with_dead_zone (extended.leftThumbstick.yAxis.value);
        const float dpad_x = axis_with_dead_zone (extended.dpad.xAxis.value);
        const float dpad_y = axis_with_dead_zone (extended.dpad.yAxis.value);
        controls.steer = stick_x != 0 ? stick_x : dpad_x;
        controls.drive = stick_y != 0 ? stick_y : dpad_y;
        controls.boost =
          std::max (extended.rightTrigger.value, extended.buttonY.value);

        edge (0, button_down (extended.buttonA), Key::E);
        edge (1, button_down (extended.buttonA), Key::Restart);
        edge (2, button_down (extended.buttonB), Key::Mount);
        edge (3, button_down (extended.buttonX), Key::Tab);
        edge (8, button_down (extended.buttonY), Key::Space);
        dpad_edges (extended.dpad);
      } else if (controller.microGamepad) {
        GCMicroGamepad* remote = controller.microGamepad;
        controls.steer = axis_with_dead_zone (remote.dpad.xAxis.value);
        controls.drive = axis_with_dead_zone (remote.dpad.yAxis.value);
        controls.boost = remote.buttonA.value;
        edge (0, button_down (remote.buttonA), Key::E);
        edge (1, button_down (remote.buttonA), Key::Restart);
        edge (2, button_down (remote.buttonX), Key::Mount);
        edge (8, button_down (remote.buttonA), Key::Space);
        dpad_edges (remote.dpad);
      }
      m_game.controls (controls);
    }

    void disconnect () {
      release_all ();
      m_controller = nil;
      m_had_controller = false;
    }

  private:
    GCController* choose_controller () {
      for (GCController* controller in GCController.controllers)
        if (controller.extendedGamepad)
          return controller;
      for (GCController* controller in GCController.controllers)
        if (controller.microGamepad)
          return controller;
      return nil;
    }

    void edge (int index, bool down, Key key) {
      if (m_buttons[index] == down)
        return;
      m_buttons[index] = down;
      m_game.key (key, down);
    }

    void dpad_edges (GCControllerDirectionPad* dpad) {
      edge (4, button_down (dpad.left), Key::Left);
      edge (5, button_down (dpad.right), Key::Right);
      edge (6, button_down (dpad.up), Key::Up);
      edge (7, button_down (dpad.down), Key::Down);
    }

    void release_all () {
      static constexpr Key keys[] = { Key::E,   Key::Restart, Key::Mount,
                                      Key::Tab, Key::Left,    Key::Right,
                                      Key::Up,  Key::Down,    Key::Space };
      for (int i = 0; i < 9; ++i)
        if (m_buttons[i]) {
          m_game.key (keys[i], false);
          m_buttons[i] = false;
        }
      m_game.controls ({});
    }

    Game& m_game;
    __strong GCController* m_controller = nil;
    bool m_buttons[9] {};
    bool m_had_controller = false;
  };

  AppleGameController::AppleGameController (Game& game)
      : m_impl (std::make_unique<Impl> (game)) {}

  AppleGameController::~AppleGameController () = default;

  void AppleGameController::poll () {
    m_impl->poll ();
  }

  void AppleGameController::disconnect () {
    m_impl->disconnect ();
  }
}
