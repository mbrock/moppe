#ifndef MOPPE_PARTITION_HH
#define MOPPE_PARTITION_HH

#include <concepts>
#include <functional>

namespace moppe {
  // A partition is represented by its quotient map.  Two elements occupy the
  // same block exactly when the map gives them equal block values.  The block
  // type belongs to each use of the abstraction; there is no universal block
  // identifier.
  template <typename Map, typename Element, typename Block>
  concept Partition = std::equality_comparable<Block> &&
                      requires (const Map& partition, const Element& element) {
                        {
                          std::invoke (partition, element)
                        } -> std::same_as<Block>;
                      };

  template <typename Map, typename Element>
    requires Partition<Map,
                       Element,
                       std::invoke_result_t<const Map&, const Element&>>
  constexpr bool
  equivalent (const Map& partition, const Element& a, const Element& b) {
    return std::invoke (partition, a) == std::invoke (partition, b);
  }
}

#endif
