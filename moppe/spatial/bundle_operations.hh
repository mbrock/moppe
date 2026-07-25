#ifndef MOPPE_SPATIAL_BUNDLE_OPERATIONS_HH
#define MOPPE_SPATIAL_BUNDLE_OPERATIONS_HH

#include <moppe/spatial/bundle.hh>

#include <concepts>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace moppe::spatial {
  namespace detail {
    template <typename Index>
    struct NeighbourhoodProbe {
      template <typename OtherIndex, typename Influence>
        requires std::convertible_to<OtherIndex, Index>
      void operator() (OtherIndex&&, Influence&&) const;
    };
  }

  template <typename Domain>
  concept NeighbourhoodDomain =
    FiniteDomain<Domain> &&
    requires (const Domain& domain, typename Domain::index_type index) {
      domain.visit_neighbourhood (
        index, detail::NeighbourhoodProbe<typename Domain::index_type> {});
    };

  template <typename Policy, typename Domain>
  concept NeighbourhoodPolicy =
    FiniteDomain<Domain> && requires (Policy policy,
                                      const Domain& domain,
                                      typename Domain::index_type index) {
      std::invoke (policy,
                   domain,
                   index,
                   detail::NeighbourhoodProbe<typename Domain::index_type> {});
    };

  // A focus is one bundle row together with its place in the domain. It is
  // the input to generic local operations, not part of Bundle's storage API.
  template <typename BundleType>
  class BundleFocus {
  public:
    using bundle_type = std::remove_const_t<BundleType>;
    using index_type = typename bundle_type::index_type;

    BundleFocus (BundleType& bundle, index_type index)
        : m_bundle (&bundle), m_index (index) {}

    index_type index () const noexcept {
      return m_index;
    }

    decltype (auto) domain () const noexcept {
      return m_bundle->domain ();
    }

    auto row () const {
      return (*m_bundle)[m_index];
    }

    auto row (index_type index) const {
      return (*m_bundle)[index];
    }

  private:
    BundleType* m_bundle;
    index_type m_index;
  };

  template <std::size_t Index, typename BundleType>
  decltype (auto) get (const BundleFocus<BundleType>& focus) {
    return get<Index> (focus.row ());
  }

  template <mp_units::QuantitySpec auto QS, typename BundleType>
    requires BundleContains<QS, typename BundleFocus<BundleType>::bundle_type>
  decltype (auto) get (const BundleFocus<BundleType>& focus) {
    using B = typename BundleFocus<BundleType>::bundle_type;
    return get<B::template spec_index<QS>> (focus);
  }

  inline constexpr struct adjacent_neighbourhood_t {
    template <typename Domain, typename Index, typename Visitor>
      requires NeighbourhoodDomain<Domain>
    void
    operator() (const Domain& domain, Index index, Visitor&& visitor) const {
      domain.visit_neighbourhood (index, std::forward<Visitor> (visitor));
    }
  } adjacent_neighbourhood;

  template <typename BundleType, typename Neighbourhood, typename Operation>
    requires NeighbourhoodPolicy<
      Neighbourhood,
      typename BundleFocus<BundleType>::bundle_type::domain_type>
  void visit_neighbourhood (const BundleFocus<BundleType>& focus,
                            Neighbourhood neighbourhood,
                            Operation operation) {
    std::invoke (neighbourhood,
                 focus.domain (),
                 focus.index (),
                 [&] (auto index, auto influence) {
                   std::invoke (operation, focus.row (index), influence);
                 });
  }

  template <typename BundleType, typename Operation>
  void visit_neighbourhood (const BundleFocus<BundleType>& focus,
                            Operation operation) {
    visit_neighbourhood (focus, adjacent_neighbourhood, std::move (operation));
  }

  template <typename BundleType,
            typename Neighbourhood,
            typename Value,
            typename Operation>
  auto fold_neighbourhood (const BundleFocus<BundleType>& focus,
                           Neighbourhood neighbourhood,
                           Value initial,
                           Operation operation) {
    visit_neighbourhood (
      focus,
      std::move (neighbourhood),
      [&] (const auto& neighbour, auto influence) {
        initial =
          std::invoke (operation, std::move (initial), neighbour, influence);
      });
    return initial;
  }

  template <typename BundleType, typename Value, typename Operation>
  auto fold_neighbourhood (const BundleFocus<BundleType>& focus,
                           Value initial,
                           Operation operation) {
    return fold_neighbourhood (focus,
                               adjacent_neighbourhood,
                               std::move (initial),
                               std::move (operation));
  }

  template <mp_units::QuantitySpec auto QS,
            typename BundleType,
            typename Neighbourhood = adjacent_neighbourhood_t>
    requires BundleContains<QS,
                            typename BundleFocus<BundleType>::bundle_type> &&
             NeighbourhoodPolicy<
               Neighbourhood,
               typename BundleFocus<BundleType>::bundle_type::domain_type>
  auto laplacian (const BundleFocus<BundleType>& focus,
                  Neighbourhood neighbourhood = adjacent_neighbourhood) {
    const auto center = get<QS> (focus);
    return fold_neighbourhood (
      focus,
      std::move (neighbourhood),
      center - center,
      [center] (auto sum, const auto& neighbour, auto influence) {
        return sum + influence * (get<QS> (neighbour) - center);
      });
  }

  template <typename... Values>
  auto bundle_values (Values&&... values) {
    return std::tuple<std::remove_cvref_t<Values>...> (
      std::forward<Values> (values)...);
  }

  namespace detail {
    template <typename Row, typename Values, std::size_t... Indices>
    void assign_bundle_row (const Row& row,
                            Values&& values,
                            std::index_sequence<Indices...>) {
      ((get<Indices> (row) = get<Indices> (std::forward<Values> (values))),
       ...);
    }
  }

  template <typename OutputDomain,
            typename... Outputs,
            typename InputDomain,
            typename... Inputs,
            typename Rule>
    requires std::same_as<OutputDomain, InputDomain>
  void extend_into (Bundle<OutputDomain, Outputs...>& output,
                    const Bundle<InputDomain, Inputs...>& input,
                    Rule rule) {
    if (output.size () != input.size ())
      throw std::invalid_argument ("Cannot extend across unequal domains");

    for (std::size_t offset = 0; offset < input.size (); ++offset) {
      const auto index = input.index (offset);
      auto values = std::invoke (rule, BundleFocus (input, index));
      static_assert (std::tuple_size_v<decltype (values)> ==
                     sizeof...(Outputs));
      detail::assign_bundle_row (output[index],
                                 std::move (values),
                                 std::index_sequence_for<Outputs...> {});
    }
  }
}

namespace std {
  template <typename BundleType>
  struct tuple_size<moppe::spatial::BundleFocus<BundleType>>
      : tuple_size<
          typename moppe::spatial::BundleFocus<BundleType>::bundle_type> {};

  template <size_t Index, typename BundleType>
  struct tuple_element<Index, moppe::spatial::BundleFocus<BundleType>>
      : tuple_element<Index, moppe::spatial::BundleRow<BundleType>> {};
}

#endif
