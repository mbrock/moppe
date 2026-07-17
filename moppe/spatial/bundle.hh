#ifndef MOPPE_SPATIAL_BUNDLE_HH
#define MOPPE_SPATIAL_BUNDLE_HH

#include <mp-units/framework.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// A Bundle is an eager, finite materialization of a heterogeneous section
// over a domain.  Values are stored by column, while BundleRow and
// BundleFocus expose one typed site to local rules.  Its finiteness is a
// storage boundary, not a claim about a more general section calculus.

namespace moppe::spatial {
  namespace detail {
    template <typename Index>
    struct NeighbourhoodProbe {
      template <typename OtherIndex, typename Influence>
        requires std::convertible_to<OtherIndex, Index>
      void operator() (OtherIndex&&, Influence&&) const;
    };

    template <typename Index>
    struct InterpolationProbe {
      template <typename OtherIndex, typename Weight>
        requires std::convertible_to<OtherIndex, Index> &&
                 std::convertible_to<Weight, float>
      void operator() (OtherIndex&&, Weight&&) const;
    };
  }

  template <typename Domain>
  concept FiniteDomain = std::move_constructible<Domain> &&
                         requires (const Domain& domain,
                                   typename Domain::index_type index,
                                   std::size_t offset) {
                           {
                             domain.size ()
                           } -> std::convertible_to<std::size_t>;
                           {
                             domain.offset (index)
                           } -> std::convertible_to<std::size_t>;
                           {
                             domain.index (offset)
                           } -> std::same_as<typename Domain::index_type>;
                         };

  template <typename Domain>
  concept NeighbourhoodDomain =
    FiniteDomain<Domain> &&
    requires (const Domain& domain, typename Domain::index_type index) {
      domain.visit_neighbourhood (
        index, detail::NeighbourhoodProbe<typename Domain::index_type> {});
    };

  template <typename Domain, typename Position>
  concept InterpolationDomain =
    FiniteDomain<Domain> &&
    requires (const Domain& domain, const Position& position) {
      domain.visit_interpolation_stencil (
        position, detail::InterpolationProbe<typename Domain::index_type> {});
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

  template <typename T>
  concept BundleValue = mp_units::Quantity<T> || mp_units::QuantityPoint<T>;

  template <typename Domain, typename... Quantities>
    requires FiniteDomain<Domain> && (BundleValue<Quantities> && ...)
  class Bundle;

  template <typename BundleType>
  class BundleRow;

  template <typename BundleType>
  class BundleFocus;

  namespace detail {
    template <auto QS, typename... Quantities>
    consteval std::size_t bundle_spec_count () {
      return (std::size_t (Quantities::quantity_spec == QS) + ... + 0);
    }

    template <auto QS, typename... Quantities>
    consteval std::size_t bundle_spec_index () {
      constexpr std::array matches { Quantities::quantity_spec == QS... };
      for (std::size_t index = 0; index < matches.size (); ++index)
        if (matches[index])
          return index;
      return matches.size ();
    }
  }

  template <typename Domain, typename... Quantities>
    requires FiniteDomain<Domain> && (BundleValue<Quantities> && ...)
  class Bundle {
  public:
    using domain_type = Domain;
    using index_type = typename Domain::index_type;

    template <std::size_t Index>
    using value_type = std::tuple_element_t<Index, std::tuple<Quantities...>>;

    template <std::size_t Index>
    using column_type = std::vector<value_type<Index>>;

    static constexpr std::size_t column_count = sizeof...(Quantities);

    template <mp_units::QuantitySpec auto QS>
    static constexpr bool contains =
      detail::bundle_spec_count<QS, Quantities...> () == 1;

    template <mp_units::QuantitySpec auto QS>
      requires contains<QS>
    static constexpr std::size_t spec_index =
      detail::bundle_spec_index<QS, Quantities...> ();

    Bundle ()
      requires std::default_initializable<Domain>
        : Bundle (Domain {}) {}

    explicit Bundle (Domain domain)
        : m_domain (std::move (domain)),
          m_columns (std::vector<Quantities> (m_domain.size ())...) {
      static_assert (
        ((detail::bundle_spec_count<Quantities::quantity_spec,
                                    Quantities...> () == 1) &&
         ...),
        "A Bundle row cannot contain the same quantity specification twice");
    }

    const Domain& domain () const noexcept {
      return m_domain;
    }

    std::size_t size () const noexcept {
      return m_domain.size ();
    }

    index_type index (std::size_t offset) const {
      return m_domain.index (offset);
    }

    template <std::size_t Index>
    column_type<Index>& column () noexcept {
      return std::get<Index> (m_columns);
    }

    template <std::size_t Index>
    const column_type<Index>& column () const noexcept {
      return std::get<Index> (m_columns);
    }

    BundleRow<Bundle> operator[] (index_type index) {
      return BundleRow<Bundle> (*this, m_domain.offset (index));
    }

    BundleRow<const Bundle> operator[] (index_type index) const {
      return BundleRow<const Bundle> (*this, m_domain.offset (index));
    }

  private:
    Domain m_domain;
    std::tuple<std::vector<Quantities>...> m_columns;
  };

  template <typename BundleType>
  class BundleRow {
  public:
    using bundle_type = std::remove_const_t<BundleType>;

    BundleRow (BundleType& bundle, std::size_t offset)
        : m_bundle (&bundle), m_offset (offset) {}

    template <std::size_t Index>
    decltype (auto) value () const {
      return get<Index> (*m_bundle)[m_offset];
    }

  private:
    BundleType* m_bundle;
    std::size_t m_offset;
  };

  // A focus is a row together with its place in the topological domain.
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

  template <auto QS, typename BundleType>
  concept BundleContains = BundleType::template contains<QS>;

  template <std::size_t Index, typename Domain, typename... Quantities>
  decltype (auto) get (Bundle<Domain, Quantities...>& bundle) noexcept {
    return bundle.template column<Index> ();
  }

  template <std::size_t Index, typename Domain, typename... Quantities>
  decltype (auto) get (const Bundle<Domain, Quantities...>& bundle) noexcept {
    return bundle.template column<Index> ();
  }

  template <mp_units::QuantitySpec auto QS,
            typename Domain,
            typename... Quantities>
    requires BundleContains<QS, Bundle<Domain, Quantities...>>
  decltype (auto) get (Bundle<Domain, Quantities...>& bundle) noexcept {
    return get<Bundle<Domain, Quantities...>::template spec_index<QS>> (bundle);
  }

  template <mp_units::QuantitySpec auto QS,
            typename Domain,
            typename... Quantities>
    requires BundleContains<QS, Bundle<Domain, Quantities...>>
  decltype (auto) get (const Bundle<Domain, Quantities...>& bundle) noexcept {
    return get<Bundle<Domain, Quantities...>::template spec_index<QS>> (bundle);
  }

  template <std::size_t Index, typename BundleType>
  decltype (auto) get (const BundleRow<BundleType>& row) {
    return row.template value<Index> ();
  }

  template <mp_units::QuantitySpec auto QS, typename BundleType>
    requires BundleContains<QS, typename BundleRow<BundleType>::bundle_type>
  decltype (auto) get (const BundleRow<BundleType>& row) {
    using B = typename BundleRow<BundleType>::bundle_type;
    return get<B::template spec_index<QS>> (row);
  }

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

  // Reconstruct a continuously sampled value from a finite bundle.  The
  // mp-units category chooses the algebra: quantities form an ordinary
  // weighted sum, while quantity points are reconstructed affinely from one
  // anchor and weighted point differences.
  template <mp_units::QuantitySpec auto QS,
            typename Domain,
            typename... Quantities,
            typename Position>
    requires InterpolationDomain<Domain, Position> &&
             BundleContains<QS, Bundle<Domain, Quantities...>>
  auto sample (const Bundle<Domain, Quantities...>& bundle,
               const Position& position) {
    using B = Bundle<Domain, Quantities...>;
    using Value = typename B::template value_type<B::template spec_index<QS>>;

    if constexpr (mp_units::QuantityPoint<Value>) {
      std::optional<Value> anchor;
      using Difference =
        decltype (std::declval<Value> () - std::declval<Value> ());
      Difference offset {};
      bundle.domain ().visit_interpolation_stencil (
        position, [&] (auto index, float weight) {
          const Value& value = get<QS> (bundle[index]);
          if (!anchor)
            anchor = value;
          offset += weight * (value - *anchor);
        });
      if (!anchor)
        throw std::logic_error ("Interpolation stencil is empty");
      return *anchor + offset;
    } else {
      Value result {};
      bool sampled = false;
      bundle.domain ().visit_interpolation_stencil (
        position, [&] (auto index, float weight) {
          result += weight * get<QS> (bundle[index]);
          sampled = true;
        });
      if (!sampled)
        throw std::logic_error ("Interpolation stencil is empty");
      return result;
    }
  }
}

namespace std {
  template <typename Domain, typename... Quantities>
  struct tuple_size<moppe::spatial::Bundle<Domain, Quantities...>>
      : integral_constant<size_t, sizeof...(Quantities)> {};

  template <size_t Index, typename Domain, typename... Quantities>
  struct tuple_element<Index, moppe::spatial::Bundle<Domain, Quantities...>> {
    using type = typename moppe::spatial::Bundle<Domain, Quantities...>::
      template column_type<Index>;
  };

  template <typename BundleType>
  struct tuple_size<moppe::spatial::BundleRow<BundleType>>
      : tuple_size<
          typename moppe::spatial::BundleRow<BundleType>::bundle_type> {};

  template <size_t Index, typename BundleType>
  struct tuple_element<Index, moppe::spatial::BundleRow<BundleType>> {
    using type =
      typename tuple_element<Index,
                             typename moppe::spatial::BundleRow<
                               BundleType>::bundle_type>::type::value_type;
  };

  template <typename BundleType>
  struct tuple_size<moppe::spatial::BundleFocus<BundleType>>
      : tuple_size<
          typename moppe::spatial::BundleFocus<BundleType>::bundle_type> {};

  template <size_t Index, typename BundleType>
  struct tuple_element<Index, moppe::spatial::BundleFocus<BundleType>>
      : tuple_element<Index, moppe::spatial::BundleRow<BundleType>> {};
}

#endif
