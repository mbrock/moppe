#include <moppe/game/machine_controls.hh>

namespace moppe::game {
  namespace {
    // The thumb counts as down past a tenth of its travel.
    bool held (proportion_t thumb) {
      return thumb > 0.1f * proportion[one];
    }
  }

  // What boosts the bike brakes a car.
  void drive (mov::Car& car, const Controls& k) {
    car.set_turn (k.turn);
    car.set_drive (k.go);
    car.set_brake (held (k.boost));
  }

  void drive (mov::Tractor& tractor, const Controls& k) {
    tractor.set_turn (k.turn);
    tractor.set_drive (k.go);
    tractor.set_brake (held (k.boost));
  }

  void drive (mov::Helicopter& heli, const Controls& k) {
    heli.set_turn (k.turn);
    heli.set_drive (k.go);
    heli.set_lift (k.lift);
  }

  // The thumb opens the boat's tied-back governor.
  void drive (mov::Boat& boat, const Controls& k) {
    boat.set_turn (k.turn);
    boat.set_drive (k.go);
    boat.set_boost (k.boost);
  }

  void drive (mov::JetSki& ski, const Controls& k) {
    ski.set_turn (k.turn);
    ski.set_drive (k.go);
  }

  void
  drive (mov::Vehicle& bike, Machine machine, const Controls& k, bool pressed) {
    bike.set_yaw (quantity_cast<isq::angular_measure> ((90 * k.turn) * u::deg));
    bike.set_thrust (k.go);
    bike.set_skid ((k.skid ? 1 : 0) * control_signal[one]);
    switch (machine) {
    case Machine::bike:
      bike.set_boost (k.boost, k.go);
      bike.set_wheelie ((k.wheelie ? 1 : 0) * control_signal[one]);
      break;
    case Machine::mountain_bike:
      // No jets on a mountainbike: the rider springs it up off the
      // ground instead.
      if (pressed)
        bike.hop (3.0f * u::m / u::s);
      break;
    default:
      break;
    }
  }

  void drive (Fleet& fleet, Machine machine, const Controls& k, bool pressed) {
    visit (fleet, machine, [&] (Machine m, auto& body) {
      if constexpr (std::is_same_v<decltype (body), mov::Vehicle&>)
        drive (body, m, k, pressed);
      else
        drive (body, k);
    });
  }
}
