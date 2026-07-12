#ifndef MOPPE_QUANTITIES_HH
#define MOPPE_QUANTITIES_HH

#include <mp-units/framework.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

namespace moppe {

  using mp_units::non_negative;
  using mp_units::quantity_spec;

  using meters_t = mp_units::quantity<mp_units::si::metre, float>;
  using square_meters_t =
    mp_units::quantity<mp_units::si::metre * mp_units::si::metre, float>;
  using cubic_meters_t =
    mp_units::quantity<mp_units::si::metre * mp_units::si::metre *
                         mp_units::si::metre,
                       float>;
  using meters_per_second_t =
    mp_units::quantity<mp_units::si::metre / mp_units::si::second, float>;

  inline float meters_value (meters_t value) {
    return value.numerical_value_in (mp_units::si::metre);
  }

  inline float square_meters_value (square_meters_t value) {
    return value.numerical_value_in (mp_units::si::metre * mp_units::si::metre);
  }

  inline float cubic_meters_value (cubic_meters_t value) {
    return value.numerical_value_in (mp_units::si::metre * mp_units::si::metre *
                                     mp_units::si::metre);
  }

  inline float meters_per_second_value (meters_per_second_t value) {
    return value.numerical_value_in (mp_units::si::metre /
                                     mp_units::si::second);
  }

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

  // The dimensions of a modeled spatial domain.  It has the vector
  // character and length dimension of ISQ displacement, but is not a
  // displacement undergone by an object.
  inline constexpr struct spatial_extent
      : quantity_spec<mp_units::isq::displacement> {
  } spatial_extent;
}

#endif
