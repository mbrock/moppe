#ifndef MOPPE_SPATIAL_BUNDLE_HH
#define MOPPE_SPATIAL_BUNDLE_HH

#include <mp-units/framework.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// A Bundle is an eager, finite store of heterogeneous typed columns over one
// domain. BundleRow exposes one site without materializing a tuple. Generic
// local rules over bundle foci live in bundle_operations.hh.

namespace moppe::spatial {
  namespace detail {
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

  template <typename Domain, typename Position>
  concept InterpolationDomain =
    FiniteDomain<Domain> &&
    requires (const Domain& domain, const Position& position) {
      domain.visit_interpolation_stencil (
        position, detail::InterpolationProbe<typename Domain::index_type> {});
    };

  template <typename T>
  concept BundleValue = mp_units::Quantity<T> || mp_units::QuantityPoint<T>;

  template <typename Domain, typename... Quantities>
    requires FiniteDomain<Domain> && (BundleValue<Quantities> && ...)
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
      validate_specs ();
    }

    Bundle (Domain domain, std::vector<Quantities>... columns)
        : m_domain (std::move (domain)), m_columns (std::move (columns)...) {
      validate_specs ();
      const bool sizes_match = std::apply (
        [this] (const auto&... column) {
          return ((column.size () == m_domain.size ()) && ...);
        },
        m_columns);
      if (!sizes_match)
        throw std::invalid_argument (
          "bundle columns need one value per domain site");
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
    static consteval void validate_specs () {
      static_assert (
        ((detail::bundle_spec_count<Quantities::quantity_spec,
                                    Quantities...> () == 1) &&
         ...),
        "A Bundle row cannot contain the same quantity specification twice");
    }

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

  // Bundles over one domain compose by carrying their columns side by side,
  // so an analysis can produce its own narrow result and hand it to a wider
  // store afterwards. Joining consumes its inputs and moves their columns;
  // repeating a quantity between them is a compile error, since a bundle row
  // holds each specification once.
  template <typename Domain, typename... Left, typename... Right>
    requires std::equality_comparable<Domain>
  Bundle<Domain, Left..., Right...> join (Bundle<Domain, Left...> left,
                                          Bundle<Domain, Right...> right) {
    if (!(left.domain () == right.domain ()))
      throw std::invalid_argument ("joined bundles need one shared domain");
    return [&]<std::size_t... LeftColumn, std::size_t... RightColumn> (
             std::index_sequence<LeftColumn...>,
             std::index_sequence<RightColumn...>) {
      return Bundle<Domain, Left..., Right...> (
        left.domain (),
        std::move (get<LeftColumn> (left))...,
        std::move (get<RightColumn> (right))...);
    }(std::index_sequence_for<Left...> {},
           std::index_sequence_for<Right...> {});
  }

  template <typename First, typename Second, typename Third, typename... Rest>
  auto join (First first, Second second, Third third, Rest... rest) {
    return join (join (std::move (first), std::move (second)),
                 std::move (third),
                 std::move (rest)...);
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

}

#endif
