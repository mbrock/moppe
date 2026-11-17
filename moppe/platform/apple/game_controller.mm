#import <GameController/GameController.h>

#include <moppe/platform/apple/game_controller.hh>
#include <moppe/platform/input.hh>
#include <moppe/platform/platform.hh>

#include <iostream>

namespace moppe::platform {
  namespace {
    bool button_down (GCControllerButtonInput* button) {
      return button && button.pressed;
    }

    void read_dpad (GCControllerDirectionPad* dpad, GamepadReading& r) {
      r.dpad_left = button_down (dpad.left);
      r.dpad_right = button_down (dpad.right);
      r.dpad_up = button_down (dpad.up);
      r.dpad_down = button_down (dpad.down);
    }
  }

  // Reads the first Apple controller into the shared mapping (input.hh).
  class AppleGameController::Impl {
  public:
    explicit Impl (Game& game) : m_game (game), m_mapper (game) {}

    void poll () {
      GCController* controller = choose_controller ();
      if (controller != m_controller) {
        m_mapper.release ();
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

      using mp_units::one;
      // The controller's axes and triggers as the game's signals.
      const auto stick = [] (GCControllerAxisInput* axis) {
        return axis.value * control_signal[one];
      };
      const auto trigger = [] (GCControllerButtonInput* button) {
        return button.value * proportion[one];
      };
      GamepadReading r;
      if (GCExtendedGamepad* pad = controller.extendedGamepad) {
        r.left_x = stick (pad.leftThumbstick.xAxis);
        r.left_y = stick (pad.leftThumbstick.yAxis);
        r.right_x = stick (pad.rightThumbstick.xAxis);
        r.right_y = stick (pad.rightThumbstick.yAxis);
        r.left_trigger = trigger (pad.leftTrigger);
        r.right_trigger = trigger (pad.rightTrigger);
        r.a = button_down (pad.buttonA);
        r.b = button_down (pad.buttonB);
        r.x = button_down (pad.buttonX);
        r.y = button_down (pad.buttonY);
        r.left_shoulder = button_down (pad.leftShoulder);
        r.right_shoulder = button_down (pad.rightShoulder);
        read_dpad (pad.dpad, r);
      } else if (GCMicroGamepad* remote = controller.microGamepad) {
        // The Siri Remote: its touch surface steers and drives, its play
        // button deploys, restarts, boosts, and skips, and its X mounts.
        r.left_x = stick (remote.dpad.xAxis);
        r.left_y = stick (remote.dpad.yAxis);
        r.a = r.y = button_down (remote.buttonA);
        r.b = button_down (remote.buttonX);
        read_dpad (remote.dpad, r);
      }
      m_game.controls (m_mapper.map (r));
    }

    void disconnect () {
      m_mapper.release ();
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

    Game& m_game;
    GamepadMapper m_mapper;
    __strong GCController* m_controller = nil;
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
