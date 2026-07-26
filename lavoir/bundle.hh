#pragma once

#include "lavoir/buffer.hh"

#include <concepts>
#include <cstddef>
#include <span>

/// The field vocabulary begins here as concepts, not base classes. A
/// Domain names sites and converts them to storage offsets; a Bundle
/// is anything that has one. Concrete fields borrow their values from
/// columns owned elsewhere, in keeping with the workshop's one rule of
/// tenure: storage is held high and lent downward.

namespace lavoir {
  template <typename D>
  concept Domain = std::copyable<D> &&
                   requires (const D& domain, typename D::index_type index) {
                     { domain.size () } -> std::convertible_to<std::size_t>;
                     {
                       domain.offset (index)
                     } -> std::convertible_to<std::size_t>;
                     {
                       domain.index (std::size_t {})
                     } -> std::same_as<typename D::index_type>;
                   };

  template <typename B>
  concept Bundle = requires (const B& bundle) {
    requires Domain<typename B::domain_type>;
    { bundle.domain () } -> std::convertible_to<const typename B::domain_type&>;
  };

  /// The simplest domain: sites 0..count, offsets equal to indices.
  /// Enough for columns that are plain sequences, and the base case
  /// every richer lattice or hive refines.
  class interval {
  public:
    using index_type = std::size_t;

    constexpr interval () = default;

    constexpr explicit interval (std::size_t count) : m_count (count) {}

    constexpr std::size_t size () const {
      return m_count;
    }

    constexpr std::size_t offset (index_type index) const {
      return index;
    }

    constexpr index_type index (std::size_t offset) const {
      return offset;
    }

  private:
    std::size_t m_count = 0;
  };

  /// One typed column of values leased over a domain. A field owns
  /// nothing: it pairs a domain with a span borrowed from a column or
  /// any other landlord, and must not outlive its lease.
  template <Domain D, typename T>
  class field {
  public:
    using domain_type = D;
    using value_type = T;

    field (D domain, std::span<T> values)
        : m_domain (std::move (domain)), m_values (values) {}

    const D& domain () const {
      return m_domain;
    }

    T& operator[] (typename D::index_type index) const {
      return m_values[m_domain.offset (index)];
    }

    std::span<T> values () const {
      return m_values;
    }

  private:
    D m_domain;
    std::span<T> m_values;
  };

  static_assert (Domain<interval>);
  static_assert (Bundle<field<interval, float>>);
}
