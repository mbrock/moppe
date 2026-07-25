#ifndef MOPPE_SPATIAL_BUNDLE_STORAGE_HH
#define MOPPE_SPATIAL_BUNDLE_STORAGE_HH

#include <moppe/spatial/bundle.hh>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
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
    inline constexpr std::string_view bundle_magic = "MOPBNDL2";

    template <typename Scalar>
    void write_scalar (std::ostream& out, Scalar value) {
      out.write (reinterpret_cast<const char*> (&value), sizeof (value));
    }

    template <typename Scalar>
    bool read_scalar (std::istream& in, Scalar& value) {
      in.read (reinterpret_cast<char*> (&value), sizeof (value));
      return static_cast<bool> (in);
    }

    inline bool read_line (std::istream& in, std::string_view expected) {
      std::string line;
      return static_cast<bool> (std::getline (in, line)) && line == expected;
    }

    inline bool read_sites (std::istream& in, std::uint64_t& sites) {
      std::string line;
      if (!std::getline (in, line))
        return false;
      constexpr std::string_view prefix = "sites=";
      if (!line.starts_with (prefix))
        return false;
      const std::string_view number (line.data () + prefix.size (),
                                     line.size () - prefix.size ());
      const auto [end, error] =
        std::from_chars (number.begin (), number.end (), sites);
      return error == std::errc {} && end == number.end ();
    }

    template <typename Value>
    std::string column_description () {
      constexpr std::string_view kind =
        mp_units::QuantityPoint<Value> ? "point" : "quantity";
      return std::format (
        "quantity kind={} bytes={} unit=[{:P}] dimension=[{:P}]",
        kind,
        sizeof (Value),
        Value::unit,
        Value::dimension);
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
    out << detail::bundle_magic << '\n';
    out << std::format ("columns={}\n", sizeof...(Quantities));
    ((out << detail::column_description<Quantities> () << '\n'), ...);
    out << std::format ("sites={}\n", bundle.size ());
    out << "data\n";
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
      if (!detail::read_line (in, detail::bundle_magic) ||
          !detail::read_line (
            in, std::format ("columns={}", sizeof...(Quantities))))
        return std::nullopt;

      const bool columns_match =
        (detail::read_line (in, detail::column_description<Quantities> ()) &&
         ...);
      if (!columns_match)
        return std::nullopt;

      std::uint64_t stored_sites = 0;
      if (!detail::read_sites (in, stored_sites) ||
          !detail::read_line (in, "data"))
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
