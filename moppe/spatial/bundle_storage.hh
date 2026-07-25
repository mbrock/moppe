#ifndef MOPPE_SPATIAL_BUNDLE_STORAGE_HH
#define MOPPE_SPATIAL_BUNDLE_STORAGE_HH

#include <moppe/spatial/bundle.hh>

#include <nanoarrow_ipc.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <istream>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// A bundle is a finite store of typed columns over one domain. It is written
// as one Arrow IPC stream record batch: scalar representations are Arrow
// numeric arrays, vector representations are fixed-size lists, and unusual
// trivially-copyable representations remain available as fixed-size binary.
//
// Units and quantity semantics live in field metadata. The domain's generic
// binary description lives in schema metadata. Arrow makes the tabular part
// portable and inspectable; a DomainStorage specialization still decides
// what constitutes that domain's identity.

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
    inline constexpr std::string_view bundle_version = "1";
    inline constexpr std::string_view version_key = "moppe.bundle.version";
    inline constexpr std::string_view domain_key = "moppe.domain";
    inline constexpr std::string_view domain_type_key = "moppe.domain.type";
    inline constexpr std::string_view kind_key = "moppe.quantity.kind";
    inline constexpr std::string_view unit_key = "moppe.quantity.unit";
    inline constexpr std::string_view dimension_key =
      "moppe.quantity.dimension";
    inline constexpr std::string_view spec_key = "moppe.quantity.spec";
    inline constexpr std::string_view storage_key = "moppe.quantity.storage";

    template <typename Scalar>
    void write_scalar (std::ostream& out, Scalar value) {
      out.write (reinterpret_cast<const char*> (&value), sizeof (value));
    }

    template <typename Scalar>
    bool read_scalar (std::istream& in, Scalar& value) {
      in.read (reinterpret_cast<char*> (&value), sizeof (value));
      return static_cast<bool> (in);
    }

    using nanoarrow::UniqueArray;
    using nanoarrow::UniqueArrayStream;
    using nanoarrow::UniqueArrayView;
    using nanoarrow::UniqueBuffer;
    using nanoarrow::UniqueSchema;
    using nanoarrow::ipc::UniqueInputStream;
    using nanoarrow::ipc::UniqueOutputStream;
    using nanoarrow::ipc::UniqueWriter;

    inline ArrowStringView arrow_string_view (std::string_view value) {
      return { value.data (), static_cast<std::int64_t> (value.size ()) };
    }

    inline ArrowBufferView arrow_buffer_view (const void* data,
                                              std::size_t size) {
      ArrowBufferView result {};
      result.data.data = data;
      result.size_bytes = static_cast<std::int64_t> (size);
      return result;
    }

    inline bool set_metadata (
      ArrowSchema& schema,
      const std::vector<std::pair<std::string_view, std::string_view>>&
        entries) {
      UniqueBuffer metadata;
      if (ArrowMetadataBuilderInit (metadata.get (), nullptr) != NANOARROW_OK)
        return false;
      for (const auto& [key, value] : entries)
        if (ArrowMetadataBuilderAppend (metadata.get (),
                                        arrow_string_view (key),
                                        arrow_string_view (value)) !=
            NANOARROW_OK)
          return false;
      return ArrowSchemaSetMetadata (
               &schema, reinterpret_cast<const char*> (metadata->data)) ==
             NANOARROW_OK;
    }

    inline std::optional<std::string_view>
    metadata_value (const ArrowSchema& schema, std::string_view key) {
      if (!schema.metadata ||
          !ArrowMetadataHasKey (schema.metadata, arrow_string_view (key)))
        return std::nullopt;
      ArrowStringView value {};
      if (ArrowMetadataGetValue (
            schema.metadata, arrow_string_view (key), &value) != NANOARROW_OK)
        return std::nullopt;
      return std::string_view (value.data,
                               static_cast<std::size_t> (value.size_bytes));
    }

    template <typename Rep>
    concept ArrowScalar =
      std::same_as<Rep, float> || std::same_as<Rep, double> ||
      (std::integral<Rep> && sizeof (Rep) <= sizeof (std::uint64_t));

    template <typename Rep>
    concept ArrowVector = requires (Rep value, std::size_t index) {
      typename Rep::value_type;
      Rep::extent;
      value[index];
    } && ArrowScalar<typename Rep::value_type>;

    template <ArrowScalar Scalar>
    consteval ArrowType arrow_scalar_type () {
      if constexpr (std::same_as<Scalar, bool>)
        return NANOARROW_TYPE_BOOL;
      else if constexpr (std::same_as<Scalar, float>)
        return NANOARROW_TYPE_FLOAT;
      else if constexpr (std::same_as<Scalar, double>)
        return NANOARROW_TYPE_DOUBLE;
      else if constexpr (std::is_signed_v<Scalar> && sizeof (Scalar) == 1)
        return NANOARROW_TYPE_INT8;
      else if constexpr (std::is_unsigned_v<Scalar> && sizeof (Scalar) == 1)
        return NANOARROW_TYPE_UINT8;
      else if constexpr (std::is_signed_v<Scalar> && sizeof (Scalar) == 2)
        return NANOARROW_TYPE_INT16;
      else if constexpr (std::is_unsigned_v<Scalar> && sizeof (Scalar) == 2)
        return NANOARROW_TYPE_UINT16;
      else if constexpr (std::is_signed_v<Scalar> && sizeof (Scalar) == 4)
        return NANOARROW_TYPE_INT32;
      else if constexpr (std::is_unsigned_v<Scalar> && sizeof (Scalar) == 4)
        return NANOARROW_TYPE_UINT32;
      else if constexpr (std::is_signed_v<Scalar> && sizeof (Scalar) == 8)
        return NANOARROW_TYPE_INT64;
      else
        return NANOARROW_TYPE_UINT64;
    }

    template <typename Value>
    using value_rep = typename Value::rep;

    template <typename Value>
    constexpr const value_rep<Value>& representation (const Value& value) {
      if constexpr (mp_units::QuantityPoint<Value>)
        return value.quantity_ref_from (Value::point_origin)
          .numerical_value_ref_in (Value::unit);
      else
        return value.numerical_value_ref_in (Value::unit);
    }

    template <typename Value>
    Value value_from_representation (value_rep<Value> representation) {
      if constexpr (mp_units::QuantityPoint<Value>) {
        typename Value::quantity_type quantity (std::move (representation),
                                                Value::reference);
        return Value (std::move (quantity), Value::point_origin);
      } else {
        return Value (std::move (representation), Value::reference);
      }
    }

    template <typename Value>
    std::string quantity_spec_name () {
      using quantity_spec =
        std::remove_cvref_t<decltype (Value::quantity_spec)>;
      return std::string (mp_units::detail::type_name<quantity_spec> ());
    }

    template <typename Value>
    std::string quantity_kind () {
      return mp_units::QuantityPoint<Value> ? "point" : "quantity";
    }

    template <typename Value>
    std::string storage_name () {
      using Rep = value_rep<Value>;
      if constexpr (ArrowScalar<Rep>)
        return "scalar";
      else if constexpr (ArrowVector<Rep>)
        return std::format ("vector[{}]",
                            static_cast<std::size_t> (Rep::extent));
      else
        return std::format ("opaque[{}]", sizeof (Value));
    }

    template <typename Value>
    bool configure_column_schema (ArrowSchema& schema) {
      using Rep = value_rep<Value>;
      ArrowErrorCode status = NANOARROW_OK;
      if constexpr (ArrowScalar<Rep>) {
        status = ArrowSchemaSetType (&schema, arrow_scalar_type<Rep> ());
      } else if constexpr (ArrowVector<Rep>) {
        status =
          ArrowSchemaSetTypeFixedSize (&schema,
                                       NANOARROW_TYPE_FIXED_SIZE_LIST,
                                       static_cast<std::int32_t> (Rep::extent));
        if (status == NANOARROW_OK)
          status = ArrowSchemaSetType (
            schema.children[0], arrow_scalar_type<typename Rep::value_type> ());
      } else {
        status = ArrowSchemaSetTypeFixedSize (
          &schema,
          NANOARROW_TYPE_FIXED_SIZE_BINARY,
          static_cast<std::int32_t> (sizeof (Value)));
      }
      if (status != NANOARROW_OK)
        return false;

      const std::string spec = quantity_spec_name<Value> ();
      if (ArrowSchemaSetName (&schema, spec.c_str ()) != NANOARROW_OK)
        return false;

      const std::string kind = quantity_kind<Value> ();
      const std::string unit = std::format ("{:P}", Value::unit);
      const std::string dimension = std::format ("{:P}", Value::dimension);
      const std::string storage = storage_name<Value> ();
      return set_metadata (schema,
                           { { kind_key, kind },
                             { unit_key, unit },
                             { dimension_key, dimension },
                             { spec_key, spec },
                             { storage_key, storage } });
    }

    template <typename Domain>
    std::string domain_description (const Domain& domain) {
      std::ostringstream out (std::ios::binary);
      DomainStorage<Domain>::write (out, domain);
      return std::move (out).str ();
    }

    inline std::string hex_encode (std::string_view bytes) {
      constexpr std::string_view digits = "0123456789abcdef";
      std::string encoded;
      encoded.reserve (bytes.size () * 2);
      for (const unsigned char byte : bytes) {
        encoded.push_back (digits[byte >> 4]);
        encoded.push_back (digits[byte & 0x0f]);
      }
      return encoded;
    }

    inline std::optional<std::string> hex_decode (std::string_view encoded) {
      const auto nibble = [] (char digit) -> std::optional<unsigned char> {
        if (digit >= '0' && digit <= '9')
          return static_cast<unsigned char> (digit - '0');
        if (digit >= 'a' && digit <= 'f')
          return static_cast<unsigned char> (digit - 'a' + 10);
        return std::nullopt;
      };
      if (encoded.size () % 2 != 0)
        return std::nullopt;

      std::string bytes;
      bytes.reserve (encoded.size () / 2);
      for (std::size_t index = 0; index < encoded.size (); index += 2) {
        const std::optional high = nibble (encoded[index]);
        const std::optional low = nibble (encoded[index + 1]);
        if (!high || !low)
          return std::nullopt;
        bytes.push_back (
          static_cast<char> (static_cast<unsigned char> (*high << 4) | *low));
      }
      return bytes;
    }

    template <typename Domain>
    std::string domain_type_name () {
      return std::string (mp_units::detail::type_name<Domain> ());
    }

    template <typename Domain, typename... Quantities>
    bool configure_bundle_schema (ArrowSchema& schema,
                                  const Bundle<Domain, Quantities...>& bundle) {
      ArrowSchemaInit (&schema);
      if (ArrowSchemaSetTypeStruct (
            &schema, static_cast<std::int64_t> (sizeof...(Quantities))) !=
          NANOARROW_OK)
        return false;

      bool columns_configured = true;
      [&]<std::size_t... Column> (std::index_sequence<Column...>) {
        columns_configured =
          (configure_column_schema<Quantities> (*schema.children[Column]) &&
           ...);
      }(std::index_sequence_for<Quantities...> {});
      if (!columns_configured)
        return false;

      const std::string domain =
        hex_encode (domain_description (bundle.domain ()));
      const std::string domain_type = domain_type_name<Domain> ();
      return set_metadata (schema,
                           { { version_key, bundle_version },
                             { domain_type_key, domain_type },
                             { domain_key, domain } });
    }

    template <ArrowScalar Scalar>
    bool append_scalar (ArrowArray& array, Scalar value) {
      if constexpr (std::same_as<Scalar, float> || std::same_as<Scalar, double>)
        return ArrowArrayAppendDouble (&array, value) == NANOARROW_OK;
      else if constexpr (std::is_signed_v<Scalar>)
        return ArrowArrayAppendInt (&array, value) == NANOARROW_OK;
      else
        return ArrowArrayAppendUInt (&array, value) == NANOARROW_OK;
    }

    template <typename Value>
    bool append_value (ArrowArray& array, const Value& value) {
      using Rep = value_rep<Value>;
      if constexpr (ArrowScalar<Rep>) {
        return append_scalar (array, representation (value));
      } else if constexpr (ArrowVector<Rep>) {
        const Rep& vector = representation (value);
        for (std::size_t component = 0; component < Rep::extent; ++component)
          if (!append_scalar (*array.children[0], vector[component]))
            return false;
        return ArrowArrayFinishElement (&array) == NANOARROW_OK;
      } else {
        return ArrowArrayAppendBytes (
                 &array, arrow_buffer_view (&value, sizeof (Value))) ==
               NANOARROW_OK;
      }
    }

    template <typename Domain, typename... Quantities>
    bool build_bundle_array (ArrowArray& array,
                             const Bundle<Domain, Quantities...>& bundle,
                             ArrowError& error) {
      for (std::size_t row = 0; row < bundle.size (); ++row) {
        bool values_appended = true;
        [&]<std::size_t... Column> (std::index_sequence<Column...>) {
          values_appended = (append_value (*array.children[Column],
                                           get<Column> (bundle)[row]) &&
                             ...);
        }(std::index_sequence_for<Quantities...> {});
        if (!values_appended ||
            ArrowArrayFinishElement (&array) != NANOARROW_OK)
          return false;
      }
      return ArrowArrayFinishBuilding (
               &array, NANOARROW_VALIDATION_LEVEL_FULL, &error) == NANOARROW_OK;
    }

    inline bool schema_shape_matches (const ArrowSchema& actual,
                                      const ArrowSchema& expected) {
      if (!actual.format || !expected.format ||
          std::string_view (actual.format) != expected.format ||
          actual.flags != expected.flags ||
          actual.n_children != expected.n_children)
        return false;
      if ((actual.name || expected.name) &&
          (!actual.name || !expected.name ||
           std::string_view (actual.name) != expected.name))
        return false;
      for (std::int64_t child = 0; child < actual.n_children; ++child)
        if (!schema_shape_matches (*actual.children[child],
                                   *expected.children[child]))
          return false;
      return true;
    }

    template <typename Value>
    bool column_schema_matches (const ArrowSchema& actual) {
      UniqueSchema expected;
      ArrowSchemaInit (expected.get ());
      if (!configure_column_schema<Value> (*expected.get ()) ||
          !schema_shape_matches (actual, *expected.get ()))
        return false;

      const std::string kind = quantity_kind<Value> ();
      const std::string unit = std::format ("{:P}", Value::unit);
      const std::string dimension = std::format ("{:P}", Value::dimension);
      const std::string spec = quantity_spec_name<Value> ();
      const std::string storage = storage_name<Value> ();
      return metadata_value (actual, kind_key) == kind &&
             metadata_value (actual, unit_key) == unit &&
             metadata_value (actual, dimension_key) == dimension &&
             metadata_value (actual, spec_key) == spec &&
             metadata_value (actual, storage_key) == storage;
    }

    template <ArrowScalar Scalar>
    Scalar read_scalar (const ArrowArrayView& view, std::int64_t index) {
      if constexpr (std::same_as<Scalar, float> || std::same_as<Scalar, double>)
        return static_cast<Scalar> (
          ArrowArrayViewGetDoubleUnsafe (&view, index));
      else if constexpr (std::is_signed_v<Scalar>)
        return static_cast<Scalar> (ArrowArrayViewGetIntUnsafe (&view, index));
      else
        return static_cast<Scalar> (ArrowArrayViewGetUIntUnsafe (&view, index));
    }

    template <typename Value>
    bool
    read_value (const ArrowArrayView& view, std::int64_t row, Value& value) {
      if (ArrowArrayViewIsNull (&view, row))
        return false;

      using Rep = value_rep<Value>;
      if constexpr (ArrowScalar<Rep>) {
        value = value_from_representation<Value> (read_scalar<Rep> (view, row));
      } else if constexpr (ArrowVector<Rep>) {
        Rep representation {};
        for (std::size_t component = 0; component < Rep::extent; ++component) {
          const std::int64_t index =
            row * static_cast<std::int64_t> (Rep::extent) +
            static_cast<std::int64_t> (component);
          if (ArrowArrayViewIsNull (view.children[0], index))
            return false;
          representation[component] =
            read_scalar<typename Rep::value_type> (*view.children[0], index);
        }
        value = value_from_representation<Value> (std::move (representation));
      } else {
        const ArrowBufferView bytes = ArrowArrayViewGetBytesUnsafe (&view, row);
        if (bytes.size_bytes != sizeof (Value))
          return false;
        std::memcpy (&value, bytes.data.data, sizeof (Value));
      }
      return true;
    }

    template <typename Domain>
    std::optional<Domain> read_domain (const ArrowSchema& schema) {
      if (metadata_value (schema, version_key) != bundle_version ||
          metadata_value (schema, domain_type_key) !=
            domain_type_name<Domain> ())
        return std::nullopt;

      const std::optional description = metadata_value (schema, domain_key);
      if (!description)
        return std::nullopt;
      const std::optional decoded = hex_decode (*description);
      if (!decoded)
        return std::nullopt;
      std::istringstream in (*decoded, std::ios::in | std::ios::binary);
      std::optional<Domain> domain = DomainStorage<Domain>::read (in);
      if (!domain || in.peek () != std::char_traits<char>::eof ())
        return std::nullopt;
      return domain;
    }

    inline bool read_ipc_stream (std::istream& in, UniqueArrayStream& stream) {
      const std::string bytes ((std::istreambuf_iterator<char> (in)),
                               std::istreambuf_iterator<char> ());
      if (bytes.empty ())
        return false;

      UniqueBuffer input;
      if (ArrowBufferAppend (input.get (), bytes.data (), bytes.size ()) !=
          NANOARROW_OK)
        return false;

      UniqueInputStream ipc_input;
      if (ArrowIpcInputStreamInitBuffer (ipc_input.get (), input.get ()) !=
          NANOARROW_OK)
        return false;
      if (ArrowIpcArrayStreamReaderInit (
            stream.get (), ipc_input.get (), nullptr) != NANOARROW_OK)
        return false;
      return true;
    }
  }

  template <typename Domain, typename... Quantities>
    requires StorableDomain<Domain> && (StorableValue<Quantities> && ...)
  void write_bundle (std::ostream& out,
                     const Bundle<Domain, Quantities...>& bundle) {
    detail::UniqueSchema schema;
    if (!detail::configure_bundle_schema (*schema.get (), bundle)) {
      out.setstate (std::ios::failbit);
      return;
    }

    ArrowError error;
    ArrowErrorInit (&error);
    detail::UniqueArray array;
    if (ArrowArrayInitFromSchema (array.get (), schema.get (), &error) !=
          NANOARROW_OK ||
        !detail::build_bundle_array (*array.get (), bundle, error)) {
      out.setstate (std::ios::failbit);
      return;
    }

    detail::UniqueArrayView array_view;
    if (ArrowArrayViewInitFromSchema (
          array_view.get (), schema.get (), &error) != NANOARROW_OK) {
      out.setstate (std::ios::failbit);
      return;
    }
    if (ArrowArrayViewSetArray (array_view.get (), array.get (), &error) !=
        NANOARROW_OK) {
      out.setstate (std::ios::failbit);
      return;
    }

    detail::UniqueBuffer encoded;
    detail::UniqueOutputStream output;
    detail::UniqueWriter writer;
    if (ArrowIpcOutputStreamInitBuffer (output.get (), encoded.get ()) !=
          NANOARROW_OK ||
        ArrowIpcWriterInit (writer.get (), output.get ()) != NANOARROW_OK) {
      out.setstate (std::ios::failbit);
      return;
    }
    if (ArrowIpcWriterWriteSchema (writer.get (), schema.get (), &error) !=
          NANOARROW_OK ||
        ArrowIpcWriterWriteArrayView (
          writer.get (), array_view.get (), &error) != NANOARROW_OK ||
        ArrowIpcWriterWriteArrayView (writer.get (), nullptr, &error) !=
          NANOARROW_OK) {
      out.setstate (std::ios::failbit);
      return;
    }

    out.write (reinterpret_cast<const char*> (encoded->data),
               static_cast<std::streamsize> (encoded->size_bytes));
  }

  template <typename BundleType>
  struct BundleStorage;

  template <typename Domain, typename... Quantities>
    requires StorableDomain<Domain> && (StorableValue<Quantities> && ...)
  struct BundleStorage<Bundle<Domain, Quantities...>> {
    using bundle_type = Bundle<Domain, Quantities...>;

    static std::optional<bundle_type> read (std::istream& in) {
      detail::UniqueArrayStream stream;
      if (!detail::read_ipc_stream (in, stream))
        return std::nullopt;

      ArrowError error;
      ArrowErrorInit (&error);
      detail::UniqueSchema schema;
      if (ArrowArrayStreamGetSchema (stream.get (), schema.get (), &error) !=
            NANOARROW_OK ||
          schema->n_children != sizeof...(Quantities))
        return std::nullopt;

      const bool columns_match =
        [&]<std::size_t... Column> (std::index_sequence<Column...>) {
          return (detail::column_schema_matches<Quantities> (
                    *schema->children[Column]) &&
                  ...);
        }(std::index_sequence_for<Quantities...> {});
      if (!columns_match)
        return std::nullopt;

      std::optional<Domain> domain =
        detail::read_domain<Domain> (*schema.get ());
      if (!domain)
        return std::nullopt;

      detail::UniqueArray array;
      if (ArrowArrayStreamGetNext (stream.get (), array.get (), &error) !=
            NANOARROW_OK ||
          !array->release ||
          array->length != static_cast<std::int64_t> (domain->size ()))
        return std::nullopt;

      detail::UniqueArray end;
      if (ArrowArrayStreamGetNext (stream.get (), end.get (), &error) !=
            NANOARROW_OK ||
          end->release)
        return std::nullopt;

      detail::UniqueArrayView view;
      if (ArrowArrayViewInitFromSchema (view.get (), schema.get (), &error) !=
          NANOARROW_OK)
        return std::nullopt;
      if (ArrowArrayViewSetArray (view.get (), array.get (), &error) !=
            NANOARROW_OK ||
          ArrowArrayViewValidate (view.get (),
                                  NANOARROW_VALIDATION_LEVEL_FULL,
                                  &error) != NANOARROW_OK)
        return std::nullopt;

      bundle_type bundle (std::move (*domain));
      for (std::int64_t row = 0; row < array->length; ++row) {
        bool values_read = true;
        [&]<std::size_t... Column> (std::index_sequence<Column...>) {
          values_read =
            (detail::read_value (
               *view->children[Column],
               row,
               get<Column> (bundle)[static_cast<std::size_t> (row)]) &&
             ...);
        }(std::index_sequence_for<Quantities...> {});
        if (!values_read)
          return std::nullopt;
      }
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
