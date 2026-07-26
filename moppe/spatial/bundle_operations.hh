#ifndef MOPPE_SPATIAL_BUNDLE_OPERATIONS_HH
#define MOPPE_SPATIAL_BUNDLE_OPERATIONS_HH

#include <moppe/spatial/bundle.hh>

#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>
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
      typename Domain::influence_type;
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

  // Every position in a domain, in storage order.
  //
  // This is how a whole-field rule says "for every site": it asks the domain
  // what its positions are. Nothing downstream learns that the domain is a
  // lattice, how wide it is, or how a position becomes a linear offset --
  // which is the whole reason a bundle takes its domain as a parameter. Rules
  // written this way work over any finite domain, and no caller reconstructs
  // `row * width + column`.
  //
  // The view borrows the domain and stays valid as long as it does.
  template <FiniteDomain Domain>
  auto sites (const Domain& domain) {
    return std::views::iota (std::size_t { 0 }, domain.size ()) |
           std::views::transform ([domain = &domain] (std::size_t offset) {
             return domain->index (offset);
           });
  }

  template <typename BundleType>
    requires FiniteDomain<typename std::remove_cvref_t<BundleType>::domain_type>
  auto sites (const BundleType& bundle) {
    return sites (bundle.domain ());
  }

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

  // Visit every site of a bundle as a focus. The counterpart to
  // visit_neighbourhood: that one is local, this one is the whole field.
  template <typename BundleType, typename Operation>
  void for_each_site (BundleType& bundle, Operation&& operation) {
    for (const auto index : sites (bundle.domain ()))
      std::invoke (operation, BundleFocus<BundleType> (bundle, index));
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

  // The Laplacian of one quantity at a focus: the influence-weighted sum of
  // differences toward each neighbour. The domain's influence carries the
  // metric -- an inverse area on a spaced lattice, a plain weight on a
  // purely topological one -- so the sum's type is the influence's times
  // the quantity's, and on a metric domain the Laplacian of an elevation
  // comes out as an elevation per area. Differences also mean this works
  // over point columns: the Laplacian of points is made of displacements.
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
    using Domain = typename BundleFocus<BundleType>::bundle_type::domain_type;
    using Term = decltype (std::declval<typename Domain::influence_type> () *
                           (center - center));
    return fold_neighbourhood (
      focus,
      std::move (neighbourhood),
      Term::zero (),
      [center] (Term sum, const auto& neighbour, auto influence) {
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

    for (const auto index : sites (input)) {
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
