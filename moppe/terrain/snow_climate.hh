#pragma once

#include <moppe/gfx/math.hh>

namespace moppe::terrain::snow_climate {
  // Shares of the climatic relief above sea level. Wind exposure and
  // shelter are at most one, the centred detail noise at most one half.
  inline constexpr proportion_t onset = 0.63f * proportion[one];
  inline constexpr proportion_t lee = 0.06f * proportion[one];
  inline constexpr proportion_t shelter = 0.13f * proportion[one];
  inline constexpr proportion_t variation = 0.10f * proportion[one];
  constexpr proportion_t lowest (proportion_t maximum_shelter) {
    return onset - lee - maximum_shelter.numerical_value_in (one) * shelter -
           0.5f * variation;
  }
}
