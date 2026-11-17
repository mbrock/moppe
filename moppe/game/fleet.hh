#ifndef MOPPE_GAME_FLEET_HH
#define MOPPE_GAME_FLEET_HH

#include <moppe/game/rescue.hh>
#include <moppe/mov/boat.hh>
#include <moppe/mov/car.hh>
#include <moppe/mov/helicopter.hh>
#include <moppe/mov/jet_ski.hh>
#include <moppe/mov/tractor.hh>
#include <moppe/mov/vehicle.hh>

#include <concepts>
#include <utility>

namespace moppe::game {
  // Every machine a player has, one of each kind, in the order the
  // session steps them; Machine names them. The list is written here
  // once and worn three ways: the bodies themselves (Fleet), what a
  // checkpoint keeps of them (FleetState), and what a frame shows of
  // them.
  template <template <class> class Of>
  struct FleetOf {
    Of<mov::Vehicle> bike;
    Of<mov::Vehicle> mountain_bike;
    Of<mov::Vehicle> long_john;
    Of<mov::Car> jeep;
    Of<mov::Helicopter> helicopter;
    Of<mov::Tractor> tractor;
    Of<mov::Car> caddy;
    Of<mov::Boat> boat;
    Of<mov::JetSki> jet_ski;
  };

  // What every machine answers to, whatever it rolls, floats, or flies
  // on: where it is and how it lies, how fast it goes and how heavy it
  // is, a blow, being left parked, the trunks it may meet, and its
  // state for a checkpoint.
  template <class B>
  concept Drivable = requires (B& b,
                               const B& c,
                               Vec3 at,
                               impulse_t blow,
                               bool yes,
                               const mov::TrunkField* trunks) {
    { c.position () } -> std::convertible_to<Vec3>;
    { c.forward () } -> std::convertible_to<Vec3>;
    { c.up () } -> std::convertible_to<Vec3>;
    { c.velocity () } -> std::convertible_to<Vec3>;
    { c.point (at) } -> std::convertible_to<Vec3>;
    { c.mass () } -> std::convertible_to<kilograms_t>;
    b.push (at, blow);
    b.set_parked (yes);
    b.set_trunks (trunks);
    b.restore (c.state ());
  };
  static_assert (Drivable<mov::Vehicle>);
  static_assert (Drivable<mov::Car>);
  static_assert (Drivable<mov::Helicopter>);
  static_assert (Drivable<mov::Tractor>);
  static_assert (Drivable<mov::Boat>);
  static_assert (Drivable<mov::JetSki>);

  template <class T>
  using Itself = T;
  template <class T>
  using StateOf = typename T::State;

  using Fleet = FleetOf<Itself>;
  using FleetState = FleetOf<StateOf>;

  // Calls fn (machine, member) with the fleet's member for `machine`:
  // the one switch on Machine.
  template <class Fleetish, class F>
  decltype (auto) visit (Fleetish& fleet, Machine machine, F&& fn) {
    switch (machine) {
    case Machine::bike:
      return fn (Machine::bike, fleet.bike);
    case Machine::jeep:
      return fn (Machine::jeep, fleet.jeep);
    case Machine::tractor:
      return fn (Machine::tractor, fleet.tractor);
    case Machine::caddy:
      return fn (Machine::caddy, fleet.caddy);
    case Machine::helicopter:
      return fn (Machine::helicopter, fleet.helicopter);
    case Machine::mountain_bike:
      return fn (Machine::mountain_bike, fleet.mountain_bike);
    case Machine::boat:
      return fn (Machine::boat, fleet.boat);
    case Machine::long_john:
      return fn (Machine::long_john, fleet.long_john);
    case Machine::jet_ski:
      return fn (Machine::jet_ski, fleet.jet_ski);
    }
    std::unreachable ();
  }

  // Calls fn (machine, member of each fleet...) for each machine, in
  // the fleet's order: fleets worn alike or not, side by side.
  template <class F, class... Fleets>
  void for_each_machine_across (F&& fn, Fleets&... fleets) {
    fn (Machine::bike, fleets.bike...);
    fn (Machine::mountain_bike, fleets.mountain_bike...);
    fn (Machine::long_john, fleets.long_john...);
    fn (Machine::jeep, fleets.jeep...);
    fn (Machine::helicopter, fleets.helicopter...);
    fn (Machine::tractor, fleets.tractor...);
    fn (Machine::caddy, fleets.caddy...);
    fn (Machine::boat, fleets.boat...);
    fn (Machine::jet_ski, fleets.jet_ski...);
  }

  // Calls fn (machine, member) for each machine, in the fleet's order.
  template <class Fleetish, class F>
  void for_each_machine (Fleetish& fleet, F&& fn) {
    for_each_machine_across (fn, fleet);
  }
}

#endif
