#ifndef MOPPE_QUANTITIES_HH
#define MOPPE_QUANTITIES_HH

#include <mp-units/framework.h>
#include <mp-units/systems/si.h>

namespace moppe {

  using mp_units::non_negative;
  using mp_units::quantity_spec;

  inline constexpr auto dimensionless = mp_units::dimensionless;

  inline constexpr struct control_signal : quantity_spec<dimensionless> {
  } control_signal;

  inline constexpr struct noise_signal : quantity_spec<dimensionless> {
  } noise_signal;

  // theoretically we could distinguish e.g. single-octave noise signals
  // so that fBm noise is constructed from such signals, which would be
  // a nice example of how to get semantic type safety from dimensionless
  // quantity types... so many interesting possibilities...

  inline constexpr struct proportion
      : quantity_spec<dimensionless, non_negative> {
  } proportion;

  inline constexpr struct probability
      : quantity_spec<dimensionless, non_negative> {
  } probability;
}

#endif
