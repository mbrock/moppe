#pragma once

#include <mp-units/framework.h>

#include <array>
#include <cstddef>
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
}
