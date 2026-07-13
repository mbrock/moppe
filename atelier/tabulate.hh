#pragma once

#include <array>
#include <cstddef>
#include <utility>

namespace atelier {
  // The array whose i-th element is f(i): tabulation as one
  // expression, in the same spirit the mp-units cartesian_vector
  // builds itself from a component function.
  template <std::size_t N>
  [[nodiscard]] constexpr auto tabulate (auto f) {
    return [&]<std::size_t... I> (std::index_sequence<I...>) {
      return std::array { f (I)... };
    }(std::make_index_sequence<N> {});
  }
}
