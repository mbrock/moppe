#ifndef MOPPE_PLATFORM_APPLE_GAME_CONTROLLER_HH
#define MOPPE_PLATFORM_APPLE_GAME_CONTROLLER_HH

#include <memory>

namespace moppe::platform {
  class Game;

  // Polls the first connected Apple Game Controller device and translates it
  // into Moppe's platform-neutral analog controls and key edges. The
  // implementation lives in Objective-C++ so game and simulation code remain
  // independent of GameController.framework.
  class AppleGameController {
  public:
    explicit AppleGameController (Game& game);
    ~AppleGameController ();

    AppleGameController (const AppleGameController&) = delete;
    AppleGameController& operator= (const AppleGameController&) = delete;

    void poll ();
    void disconnect ();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
