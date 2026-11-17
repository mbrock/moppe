#ifndef MOPPE_GAME_AFFORDANCES_HH
#define MOPPE_GAME_AFFORDANCES_HH

// What can be done just now, and with which button: one reckoning that
// the step reads to do what a pressed button does, and the HUD reads to
// prompt for it and to label a touch screen's buttons (hud.cc words it).
// Steering, the throttle, and the thumb (Space) are not here: they are held,
// not pressed, and every machine reads them its own way
// (game/machine_controls.hh).

#include <moppe/game/machine.hh>
#include <moppe/map/surface.hh>

#include <cstdint>
#include <optional>
#include <vector>

namespace moppe::game {
  class GameSession;
  struct InputFrame;
  struct Surroundings;

  // The game's few buttons, whatever the host calls them.
  enum class Button : std::uint8_t { use, mount, hammock, fire };

  // Something the player could do just now, and to what.
  struct Affordance {
    enum class Verb : std::uint8_t {
      // On foot, by hand.
      let_go_rope,
      tend_line,
      pick,
      take_egg,
      cook,
      get_up,
      lie_down,
      hang_hammock,
      take_down_hammock,
      tend_beacon,
      light_fire,
      douse_fire,
      cast,
      // The machines.
      drop_bike,
      deploy_glider,
      let_load_go,
      hook,
      haul,
      ride_along,
      take,
      step_out,
      // The bus: on at a stop, and off (or off at the next stop).
      board_bus,
      leave_bus,
      // Only said: a machine stuck.
      stuck,
    };
    Verb verb = Verb::stuck;
    // The button that does it; none for what is only said.
    std::optional<Button> button;
    // Worth a prompt of its own. What can be done almost anywhere (light
    // a fire, step out) is only offered on its button.
    bool prompted = true;
    // Pressing it does nothing but show that it will not do: a
    // poisonous mushroom, a basket full of eggs.
    bool refused = false;
    // The machine, mushroom, nest box, or beacon it is done to.
    Machine machine = Machine::bike;
    std::uint32_t object = 0;
  };

  // What can be done here, most pressing first. `around` is the world
  // the walker's hands reach into; without it only the machines are
  // reckoned with.
  std::vector<Affordance> affordances (const GameSession& session,
                                       const map::SurfaceGeometry& terrain,
                                       const Surroundings* around);

  // Does it. The one switch on Verb that acts.
  void perform (GameSession& session,
                const map::SurfaceGeometry& terrain,
                const Surroundings* around,
                const Affordance& affordance);

  // For each button `input` presses, does the first thing here that
  // button does, and takes the press from `input`.
  void press_buttons (GameSession& session,
                      const map::SurfaceGeometry& terrain,
                      const Surroundings* around,
                      InputFrame& input);
}

#endif
