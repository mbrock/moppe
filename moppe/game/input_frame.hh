#ifndef MOPPE_GAME_INPUT_FRAME_HH
#define MOPPE_GAME_INPUT_FRAME_HH

#include <moppe/gfx/math.hh>

namespace moppe::game {
  // The simulation-facing input for one tick.  It is deliberately a value
  // with no platform event or device types, so live and recorded play use the
  // same controls.
  struct InputFrame {
    control_signal_t turn {};
    control_signal_t drive {};
    proportion_t boost {};
    // The use key, pressed this tick and held (F, a controller's A), and
    // the mount key: each does the most pressing thing it can here
    // (game/affordances.hh).
    bool use = false;
    bool use_held = false;
    bool toggle_mount = false;
    bool toggle_pogo = false;
    bool cycle_camera = false;
    bool leave_cinematic = false;
    // Making camp, on foot (game/camp.hh): hang the hammock between the
    // trees at hand or take it down, and light the fire or put it out.
    bool hang_hammock = false;
    bool light_fire = false;
    // Riding the motocross, what hangs the hammock on foot (R, the left
    // shoulder) is held to lean back for a wheelie.
    bool lean_back = false;
    // Head movement since the last tick, from the mouse or the right
    // stick: on foot it turns the walker and tilts the gaze; riding or
    // gliding it looks around the way ahead.
    radians_t look_yaw {};
    radians_t look_pitch {};
    bool run = false;
  };

  inline float input_value (control_signal_t value) {
    return value.numerical_value_in (one);
  }

  inline float input_value (proportion_t value) {
    return value.numerical_value_in (one);
  }
}

#endif
