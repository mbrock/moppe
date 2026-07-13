#pragma once

#include <mp-units/framework.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// A Bundle is a heterogeneous row of quantities and quantity points repeated
// over one topological domain.  Its storage is columnar, but its labels come
// from the values' own mp-units specifications: get<QS>(bundle) is therefore
// the materialized counterpart of a typed Field<QS>.

namespace atelier {
  template <typename T>
  concept BundleValue = mp_units::Quantity<T> || mp_units::QuantityPoint<T>;

  template <typename Domain, typename... Quantities>
    requires (BundleValue<Quantities> && ...)
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
      for (std::size_t index = 0; index < matches.size (); ++index) {
        if (matches[index])
          return index;
      }
      return matches.size ();
    }
  }

  template <typename Domain, typename... Quantities>
    requires (BundleValue<Quantities> && ...)
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

    explicit Bundle (Domain domain = {})
        : m_domain (std::move (domain)),
          m_columns (std::vector<Quantities> (m_domain.size ())...) {
      static_assert (
        ((detail::bundle_spec_count<Quantities::quantity_spec,
                                    Quantities...> () == 1) &&
         ...),
        "A Bundle row cannot contain the same quantity specification twice");
    }

    [[nodiscard]] const Domain& domain () const noexcept {
      return m_domain;
    }

    [[nodiscard]] std::size_t size () const noexcept {
      return m_domain.size ();
    }

    [[nodiscard]] index_type index (std::size_t offset) const {
      return m_domain.index (offset);
    }

    template <std::size_t Index>
    [[nodiscard]] column_type<Index>& column () noexcept {
      return std::get<Index> (m_columns);
    }

    template <std::size_t Index>
    [[nodiscard]] const column_type<Index>& column () const noexcept {
      return std::get<Index> (m_columns);
    }

    [[nodiscard]] BundleRow<Bundle> operator[] (index_type index) {
      return BundleRow<Bundle> (*this, m_domain.offset (index));
    }

    [[nodiscard]] BundleRow<const Bundle> operator[] (index_type index) const {
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
    [[nodiscard]] decltype (auto) value () const {
      return get<Index> (*m_bundle)[m_offset];
    }

  private:
    BundleType* m_bundle;
    std::size_t m_offset;
  };

  // A focus is a row together with its place in the topological domain.  It
  // is the local context passed to stencil rules: ordinary get<QS> observes
  // the value at the focus, while topology-aware operations can reach out to
  // adjacent rows without exposing traversal in the rule itself.
  template <typename BundleType>
  class BundleFocus {
  public:
    using bundle_type = std::remove_const_t<BundleType>;
    using index_type = typename bundle_type::index_type;

    BundleFocus (BundleType& bundle, index_type index)
        : m_bundle (&bundle), m_index (index) {}

    [[nodiscard]] index_type index () const noexcept {
      return m_index;
    }

    [[nodiscard]] decltype (auto) domain () const noexcept {
      return m_bundle->domain ();
    }

    [[nodiscard]] auto row () const {
      return (*m_bundle)[m_index];
    }

    [[nodiscard]] auto row (index_type index) const {
      return (*m_bundle)[index];
    }

  private:
    BundleType* m_bundle;
    index_type m_index;
  };

  template <auto QS, typename BundleType>
  concept BundleContains = BundleType::template contains<QS>;

  template <std::size_t Index, typename Domain, typename... Quantities>
  [[nodiscard]] decltype (auto)
  get (Bundle<Domain, Quantities...>& bundle) noexcept {
    return bundle.template column<Index> ();
  }

  template <std::size_t Index, typename Domain, typename... Quantities>
  [[nodiscard]] decltype (auto)
  get (const Bundle<Domain, Quantities...>& bundle) noexcept {
    return bundle.template column<Index> ();
  }

  template <mp_units::QuantitySpec auto QS,
            typename Domain,
            typename... Quantities>
    requires BundleContains<QS, Bundle<Domain, Quantities...>>
  [[nodiscard]] decltype (auto)
  get (Bundle<Domain, Quantities...>& bundle) noexcept {
    return get<Bundle<Domain, Quantities...>::template spec_index<QS>> (bundle);
  }

  template <mp_units::QuantitySpec auto QS,
            typename Domain,
            typename... Quantities>
    requires BundleContains<QS, Bundle<Domain, Quantities...>>
  [[nodiscard]] decltype (auto)
  get (const Bundle<Domain, Quantities...>& bundle) noexcept {
    return get<Bundle<Domain, Quantities...>::template spec_index<QS>> (bundle);
  }

  template <std::size_t Index, typename BundleType>
  [[nodiscard]] decltype (auto) get (const BundleRow<BundleType>& row) {
    return row.template value<Index> ();
  }

  template <mp_units::QuantitySpec auto QS, typename BundleType>
    requires BundleContains<QS, typename BundleRow<BundleType>::bundle_type>
  [[nodiscard]] decltype (auto) get (const BundleRow<BundleType>& row) {
    using Bundle = BundleRow<BundleType>::bundle_type;
    return get<Bundle::template spec_index<QS>> (row);
  }

  template <std::size_t Index, typename BundleType>
  [[nodiscard]] decltype (auto) get (const BundleFocus<BundleType>& focus) {
    return get<Index> (focus.row ());
  }

  template <mp_units::QuantitySpec auto QS, typename BundleType>
    requires BundleContains<QS, typename BundleFocus<BundleType>::bundle_type>
  [[nodiscard]] decltype (auto) get (const BundleFocus<BundleType>& focus) {
    using Bundle = BundleFocus<BundleType>::bundle_type;
    return get<Bundle::template spec_index<QS>> (focus);
  }

  // A neighbourhood emits an open sequence of (index, influence) pairs.  It
  // may visit adjacent cells, a weighted radius, a ligament graph, or a
  // procedural spatial kernel; Bundle does not require that sequence to exist
  // as a container at all.
  inline constexpr struct adjacent_neighbourhood_t {
    template <typename Domain, typename Index, typename Visitor>
    void
    operator() (const Domain& domain, Index index, Visitor&& visitor) const {
      domain.visit_neighbourhood (index, std::forward<Visitor> (visitor));
    }
  } adjacent_neighbourhood;

  template <typename BundleType, typename Neighbourhood, typename Operation>
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
  [[nodiscard]] auto fold_neighbourhood (const BundleFocus<BundleType>& focus,
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
  [[nodiscard]] auto fold_neighbourhood (const BundleFocus<BundleType>& focus,
                                         Value initial,
                                         Operation operation) {
    return fold_neighbourhood (focus,
                               adjacent_neighbourhood,
                               std::move (initial),
                               std::move (operation));
  }

  // The unnormalized graph Laplacian.  Deriving zero from center - center is
  // deliberate: the result is correct for scalar and vector quantities, and
  // for a quantity_point it becomes the corresponding relative quantity.
  template <mp_units::QuantitySpec auto QS,
            typename BundleType,
            typename Neighbourhood = adjacent_neighbourhood_t>
    requires BundleContains<QS, typename BundleFocus<BundleType>::bundle_type>
  [[nodiscard]] auto
  laplacian (const BundleFocus<BundleType>& focus,
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
  [[nodiscard]] auto bundle_values (Values&&... values) {
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

  // This is the eager form of comonadic extend: evaluate one local rule at
  // every possible focus and materialize the resulting row into another
  // bundle over the same kind of domain.
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
  template <typename Domain, typename... Quantities>
  struct tuple_size<atelier::Bundle<Domain, Quantities...>>
      : integral_constant<size_t, sizeof...(Quantities)> {};

  template <size_t Index, typename Domain, typename... Quantities>
  struct tuple_element<Index, atelier::Bundle<Domain, Quantities...>> {
    using type =
      typename atelier::Bundle<Domain,
                               Quantities...>::template column_type<Index>;
  };

  template <typename BundleType>
  struct tuple_size<atelier::BundleRow<BundleType>>
      : tuple_size<typename atelier::BundleRow<BundleType>::bundle_type> {};

  template <size_t Index, typename BundleType>
  struct tuple_element<Index, atelier::BundleRow<BundleType>> {
    using type = typename tuple_element<
      Index,
      typename atelier::BundleRow<BundleType>::bundle_type>::type::value_type;
  };

  template <typename BundleType>
  struct tuple_size<atelier::BundleFocus<BundleType>>
      : tuple_size<typename atelier::BundleFocus<BundleType>::bundle_type> {};

  template <size_t Index, typename BundleType>
  struct tuple_element<Index, atelier::BundleFocus<BundleType>>
      : tuple_element<Index, atelier::BundleRow<BundleType>> {};
}
