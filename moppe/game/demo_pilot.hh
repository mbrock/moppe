// The autopilot that rides for headless verification (MOPPE_DEMO): a lazy
// arc with periodic boost-assisted leaps, weaving between the trees it
// sees ahead; the helicopter spools up, hooks what is under it, and
// cruises; `wheelie` holds the bike on its back wheel; `glide` opens the
// wing; the boat and the jet ski follow the water autopilot.
#ifndef MOPPE_GAME_DEMO_PILOT_HH
#define MOPPE_GAME_DEMO_PILOT_HH

#include <moppe/game/game_session.hh>
#include <moppe/game/input_frame.hh>
#include <moppe/game/parking.hh>
#include <moppe/game/world_ground.hh>

namespace moppe::game {
  class DemoPilot {
  public:
    // Whether the launch asked for the demo autopilot.
    static bool active ();

    // This step's controls in place of `input`, whose look the player
    // keeps.
    InputFrame steer (InputFrame input,
                      GameSession& session,
                      Parking& parking,
                      const WorldGround& ground,
                      seconds_t total_time,
                      seconds_t dt);

  private:
    // The boat (MOPPE_DEMO=boat) goes down the river's course where it
    // has one; the jet ski (MOPPE_DEMO=jet_ski) makes for the widest
    // water it can see, looks as far ahead as it covers in a few seconds,
    // and shuts the throttle to come round when the shallows are near.
    static InputFrame water (GameSession& session,
                             Parking& parking,
                             const WorldGround& ground,
                             seconds_t t,
                             bool boat);

    // MOPPE_DEMO=wheelie's last reading of the bike's pitch.
    radians_t m_pitch {};
  };
}

#endif
