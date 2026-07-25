#ifndef MOPPE_SPATIAL_BUNDLE_STORAGE_HH
#define MOPPE_SPATIAL_BUNDLE_STORAGE_HH

#include <moppe/spatial/bundle.hh>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <optional>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

// A bundle is a finite store of typed columns over one domain, so it saves
// and loads without knowing what its quantities mean: the domain describes
// itself, and every column is one run of trivially copyable values. A bundle
// that grows a column grows it in its files too, and a file whose shape no
// longer matches the type that reads it is simply rejected.
//
// The encoding is this machine's own value representation. That suits a
// local cache keyed by build and recipe; it is not a portable format.

namespace moppe::spatial {
  // Specialize for a domain that can write and recover its own identity.
  // The reader returns nothing when the stored description is not a domain.
  template <typename Domain>
  struct DomainStorage;

  template <typename Domain>
  concept StorableDomain =
    FiniteDomain<Domain> &&
    requires (std::ostream& out, std::istream& in, const Domain& domain) {
      DomainStorage<Domain>::write (out, domain);
      {
        DomainStorage<Domain>::read (in)
      } -> std::same_as<std::optional<Domain>>;
    };

  template <typename Value>
  concept StorableValue =
    BundleValue<Value> && std::is_trivially_copyable_v<Value> &&
    std::default_initializable<Value>;

  namespace detail {
    inline constexpr char bundle_magic[8] = { 'M', 'O', 'P', 'B',
                                              'N', 'D', 'L', '1' };

    template <typename Scalar>
    void write_scalar (std::ostream& out, Scalar value) {
      out.write (reinterpret_cast<const char*> (&value), sizeof (value));
    }

    template <typename Scalar>
    bool read_scalar (std::istream& in, Scalar& value) {
      in.read (reinterpret_cast<char*> (&value), sizeof (value));
      return static_cast<bool> (in);
    }

    template <typename Value>
    void write_column (std::ostream& out, const std::vector<Value>& column) {
      out.write (
        reinterpret_cast<const char*> (column.data ()),
        static_cast<std::streamsize> (column.size () * sizeof (Value)));
    }

    template <typename Value>
    bool read_column (std::istream& in, std::vector<Value>& column) {
      in.read (reinterpret_cast<char*> (column.data ()),
               static_cast<std::streamsize> (column.size () * sizeof (Value)));
      return static_cast<bool> (in);
    }
  }

  // The written shape: what the reader must agree with before it trusts a
  // byte of the columns.
  template <typename Domain, typename... Quantities>
    requires StorableDomain<Domain> && (StorableValue<Quantities> && ...)
  void write_bundle (std::ostream& out,
                     const Bundle<Domain, Quantities...>& bundle) {
    out.write (detail::bundle_magic, sizeof (detail::bundle_magic));
    detail::write_scalar (out,
                          static_cast<std::uint32_t> (sizeof...(Quantities)));
    (detail::write_scalar (out,
                           static_cast<std::uint32_t> (sizeof (Quantities))),
     ...);
    detail::write_scalar (out, static_cast<std::uint64_t> (bundle.size ()));
    DomainStorage<Domain>::write (out, bundle.domain ());
    [&]<std::size_t... Column> (std::index_sequence<Column...>) {
      (detail::write_column (out, get<Column> (bundle)), ...);
    }(std::index_sequence_for<Quantities...> {});
  }

  template <typename BundleType>
  struct BundleStorage;

  template <typename Domain, typename... Quantities>
    requires StorableDomain<Domain> && (StorableValue<Quantities> && ...)
  struct BundleStorage<Bundle<Domain, Quantities...>> {
    using bundle_type = Bundle<Domain, Quantities...>;

    static std::optional<bundle_type> read (std::istream& in) {
      char magic[sizeof (detail::bundle_magic)] {};
      in.read (magic, sizeof (magic));
      if (!in || std::memcmp (magic, detail::bundle_magic, sizeof (magic)) != 0)
        return std::nullopt;

      std::uint32_t stored_columns = 0;
      if (!detail::read_scalar (in, stored_columns) ||
          stored_columns != sizeof...(Quantities))
        return std::nullopt;
      const bool widths_match =
        (([&] {
           std::uint32_t stored_width = 0;
           return detail::read_scalar (in, stored_width) &&
                  stored_width == sizeof (Quantities);
         }()) &&
         ...);
      if (!widths_match)
        return std::nullopt;

      std::uint64_t stored_sites = 0;
      if (!detail::read_scalar (in, stored_sites))
        return std::nullopt;
      std::optional<Domain> domain = DomainStorage<Domain>::read (in);
      if (!domain || domain->size () != stored_sites)
        return std::nullopt;

      bundle_type bundle (std::move (*domain));
      const bool columns_read =
        [&]<std::size_t... Column> (std::index_sequence<Column...>) {
          return (detail::read_column (in, get<Column> (bundle)) && ...);
        }(std::index_sequence_for<Quantities...> {});
      if (!columns_read)
        return std::nullopt;
      return bundle;
    }
  };

  template <typename BundleType>
  std::optional<BundleType> read_bundle (std::istream& in) {
    return BundleStorage<BundleType>::read (in);
  }

  // Read into a bundle that already has the domain the caller means to keep.
  // A file over some other domain leaves the bundle untouched, so a caller
  // can treat a stale file as no file at all.
  template <typename Domain, typename... Quantities>
    requires StorableDomain<Domain> && std::equality_comparable<Domain> &&
             (StorableValue<Quantities> && ...)
  bool load_bundle (std::istream& in, Bundle<Domain, Quantities...>& bundle) {
    std::optional loaded = read_bundle<Bundle<Domain, Quantities...>> (in);
    if (!loaded || !(loaded->domain () == bundle.domain ()))
      return false;
    bundle = std::move (*loaded);
    return true;
  }
}

#endif
