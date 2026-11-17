#ifndef MOPPE_GAME_DOING_HH
#define MOPPE_GAME_DOING_HH

#include <moppe/game/machine.hh>

#include <cstdint>
#include <optional>

namespace moppe::game {
  // What the player is doing to get about: walking, gliding, driving
  // one of their machines (game/fleet.hh), or riding along in the other
  // player's (game/companion.hh).
  struct Doing {
    enum class Way : std::uint8_t { foot, glider, driving, passenger };
    Way way = Way::driving;
    // Driving, which machine; the bike otherwise.
    Machine machine = Machine::bike;

    static constexpr Doing on_foot () {
      return { Way::foot };
    }
    static constexpr Doing gliding () {
      return { Way::glider };
    }
    static constexpr Doing riding_along () {
      return { Way::passenger };
    }
    static constexpr Doing driving (Machine machine) {
      return { Way::driving, machine };
    }
    friend constexpr bool operator== (const Doing&, const Doing&) = default;
  };
  // The machine being driven, if any.
  constexpr std::optional<Machine> driven_machine (Doing doing) {
    if (doing.way != Doing::Way::driving)
      return std::nullopt;
    return doing.machine;
  }
  // The bikes ridden by pedalling, which have no jets.
  inline bool pedalled (Doing doing) {
    return doing.way == Doing::Way::driving &&
           machine_kind (doing.machine).pedalled;
  }
}

#endif
