#ifndef MOPPE_QUANTITIES_HH
#define MOPPE_QUANTITIES_HH

#include <mp-units/framework.h>
#include <mp-units/systems/si.h>

// Moppe's own quantity vocabulary.  "Dimensionless" is an erasure:
// ISO calls these "quantities of dimension one" precisely because
// there are many distinct ones.  A mask is not a slope is not a
// throttle, even though every one of them is stored in a float.
// These specs are non-kind children of dimensionless, so they convert
// implicitly toward plain numbers at unit-blind boundaries while
// still naming what a value means where it is declared.
namespace moppe {
  // A convex weight in [0, 1]: blend masks, moisture, cover, sway,
  // fade.  The complement (1 - w) of a proportion is a proportion;
  // a proportion of a proportion is a proportion.
  QUANTITY_SPEC (proportion, mp_units::dimensionless);

  // A proportion whose basis is an ensemble of events.  Rate * time
  // is the usual way one of these appears in a frame loop.
  QUANTITY_SPEC (probability, proportion);

  // A normalized actuator command in [-1, 1]: throttle, steering
  // drive, boost.  It amplifies a capability (gain * max_force), it
  // is not a fraction of anything.
  QUANTITY_SPEC (gain, mp_units::dimensionless);
}

#endif
