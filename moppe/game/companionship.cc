#include <moppe/game/companionship.hh>
#include <moppe/mov/boat.hh>
#include <moppe/mov/car.hh>

#include <iostream>

namespace moppe::game {
  void Companionship::notice (const char* words) {
    last_notice = words;
    notice_at = link ? link->now () : 0.0;
    std::cerr << "moppe: multiplayer: " << words << std::endl;
  }

  Vec3 Companionship::jeep_seat (const CompanionSnapshot& theirs) {
    const CarPose& jeep = theirs.jeep;
    const Vec3 left = normalized (cross (jeep.up, jeep.forward));
    return jeep.position - left * mov::jeep_rig.seat[0] +
           jeep.up * mov::jeep_rig.seat[1] +
           jeep.forward * mov::jeep_rig.seat[2];
  }

  Vec3 Companionship::boat_seat (const CompanionSnapshot& theirs) {
    const BoatPose& boat = theirs.boat;
    const Vec3 left = normalized (cross (boat.up, boat.forward));
    return boat.position + left * mov::Boat::fore[0] +
           boat.up * mov::Boat::fore[1] + boat.forward * mov::Boat::fore[2];
  }
}
