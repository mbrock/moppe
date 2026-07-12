#include <tests/test.hh>

#include <mp-units/systems/si.h>

namespace {
  using namespace mp_units;
  using namespace mp_units::si::unit_symbols;

  MOPPE_TEST (mp_units_is_available_to_project_targets) {
    const auto distance = 125 * m;
    const auto duration = 5 * s;
    const auto speed = distance / duration;

    MOPPE_CHECK (speed == 25 * m / s);
  }
}
