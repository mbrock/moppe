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
    bool deploy_glider = false;
    bool toggle_mount = false;
    bool cycle_camera = false;
    bool leave_cinematic = false;
  };

  inline float input_value (control_signal_t value) {
    return value.numerical_value_in (one);
  }

  inline float input_value (proportion_t value) {
    return value.numerical_value_in (one);
  }
}

#endif
