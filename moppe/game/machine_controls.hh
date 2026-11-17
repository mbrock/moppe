#ifndef MOPPE_GAME_MACHINE_CONTROLS_HH
#define MOPPE_GAME_MACHINE_CONTROLS_HH

#include <moppe/game/fleet.hh>

namespace moppe::game {
  // What the player's hands ask this step, whatever they are getting
  // about on: the steering (-1 left to 1 right), the drive (-1 back to 1
  // ahead), the thumb (0 to 1: what boosts the bike, brakes a car,
  // climbs the helicopter, or jumps on foot), the helicopter's lift (-1
  // down to 1 up), and what runs on foot and leans back, which stamp on
  // a bike's rear brake and lift its front wheel.
  struct Controls {
    float turn = 0.0f;
    float go = 0.0f;
    float boost = 0.0f;
    float lift = 0.0f;
    bool skid = false;
    bool wheelie = false;
  };

  // Each machine reads the controls its own way; `pressed` is whether
  // the thumb has just gone down this step. Controls{} lets go of
  // everything.
  void drive (mov::Car& car, const Controls& k);
  void drive (mov::Tractor& tractor, const Controls& k);
  void drive (mov::Helicopter& heli, const Controls& k);
  void drive (mov::Boat& boat, const Controls& k);
  void drive (mov::JetSki& ski, const Controls& k);
  // The motocross's jets, the mountainbike's hop, or neither for the
  // Long John, by which bike it is.
  void
  drive (mov::Vehicle& bike, Machine machine, const Controls& k, bool pressed);

  // Drives `machine` of the fleet.
  void drive (Fleet& fleet, Machine machine, const Controls& k, bool pressed);
}

#endif
