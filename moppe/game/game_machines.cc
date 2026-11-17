// The water autopilots (MOPPE_DEMO=boat|jet_ski), steered by Parking's
// water_autopilot along the boat's mooring.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    InputFrame MoppeGame::boat_autopilot (float t) {
      const mov::Boat& boat = session ().boat ();
      return m_parking.water_autopilot (world_ground (),
                                        t,
                                        boat.position (),
                                        boat.forward (),
                                        { .follow_route = true });
    }

    InputFrame MoppeGame::jet_ski_autopilot (float t) {
      const mov::JetSki& ski = session ().jet_ski ();
      return m_parking.water_autopilot (
        world_ground (),
        t,
        ski.position (),
        ski.forward (),
        { .speed = length (ski.velocity ()), .sight = 240.0f, .room = 200.0f });
    }
  }
}
