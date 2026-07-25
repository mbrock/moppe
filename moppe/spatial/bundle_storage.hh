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

    using Metadata = std::vector<std::pair<std::string, std::string>>;

    // ---- Raw bytes, for domains describing their own identity -------------
    // A DomainStorage specialization writes whatever fields make up that
    // domain; these are the primitives it does it with.

    template <typename Scalar>
    void write_scalar (std::ostream& out, Scalar value) {
      out.write (reinterpret_cast<const char*> (&value), sizeof (value));
    }

    template <typename Scalar>
    bool read_scalar (std::istream& in, Scalar& value) {
      in.read (reinterpret_cast<char*> (&value), sizeof (value));
      return static_cast<bool> (in);
    }

    // ---- Speaking nanoarrow -----------------------------------------------

    using nanoarrow::UniqueArray;
    using nanoarrow::UniqueArrayStream;
    using nanoarrow::UniqueArrayView;
    using nanoarrow::UniqueBuffer;
    using nanoarrow::UniqueSchema;
    using nanoarrow::ipc::UniqueInputStream;
    using nanoarrow::ipc::UniqueOutputStream;
    using nanoarrow::ipc::UniqueWriter;

    inline bool ok (ArrowErrorCode status) {
      return status == NANOARROW_OK;
    }

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

    inline bool set_metadata (ArrowSchema& schema, const Metadata& entries) {
      UniqueBuffer metadata;
      if (!ok (ArrowMetadataBuilderInit (metadata.get (), nullptr)))
        return false;
      for (const auto& [key, value] : entries)
        if (!ok (ArrowMetadataBuilderAppend (metadata.get (),
                                             arrow_string_view (key),
                                             arrow_string_view (value))))
          return false;
      return ok (ArrowSchemaSetMetadata (
        &schema, reinterpret_cast<const char*> (metadata->data)));
    }

    inline std::optional<std::string_view>
    metadata_value (const ArrowSchema& schema, std::string_view key) {
      if (!schema.metadata ||
          !ArrowMetadataHasKey (schema.metadata, arrow_string_view (key)))
        return std::nullopt;
      ArrowStringView value {};
      if (!ok (ArrowMetadataGetValue (
            schema.metadata, arrow_string_view (key), &value)))
        return std::nullopt;
      return std::string_view (value.data,
                               static_cast<std::size_t> (value.size_bytes));
    }

    inline bool metadata_matches (const ArrowSchema& schema,
                                  const Metadata& expected) {
      for (const auto& [key, value] : expected)
        if (metadata_value (schema, key) != value)
          return false;
      return true;
    }

    // ---- Quantities and their representations -----------------------------

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

    // ---- Arrow's numeric leaves -------------------------------------------

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

    template <ArrowScalar Scalar>
    bool append_scalar (ArrowArray& array, Scalar value) {
      if constexpr (std::same_as<Scalar, float> || std::same_as<Scalar, double>)
        return ok (ArrowArrayAppendDouble (&array, value));
      else if constexpr (std::is_signed_v<Scalar>)
        return ok (ArrowArrayAppendInt (&array, value));
      else
        return ok (ArrowArrayAppendUInt (&array, value));
    }

    template <ArrowScalar Scalar>
    Scalar scalar_at (const ArrowArrayView& view, std::int64_t index) {
      if constexpr (std::same_as<Scalar, float> || std::same_as<Scalar, double>)
        return static_cast<Scalar> (
          ArrowArrayViewGetDoubleUnsafe (&view, index));
      else if constexpr (std::is_signed_v<Scalar>)
        return static_cast<Scalar> (ArrowArrayViewGetIntUnsafe (&view, index));
      else
        return static_cast<Scalar> (ArrowArrayViewGetUIntUnsafe (&view, index));
    }

    // ---- One column, one encoding -----------------------------------------
    // How a quantity's representation becomes an Arrow column: each encoding
    // names itself, describes its own field, and carries values in both
    // directions. A representation Arrow has no better shape for travels as
    // its own bytes, which is what the primary template does.

    template <typename Value, typename Rep = value_rep<Value>>
    struct ColumnEncoding {
      static std::string name () {
        return std::format ("opaque[{}]", sizeof (Value));
      }

      static bool configure (ArrowSchema& schema) {
        return ok (ArrowSchemaSetTypeFixedSize (
          &schema,
          NANOARROW_TYPE_FIXED_SIZE_BINARY,
          static_cast<std::int32_t> (sizeof (Value))));
      }

      static bool append (ArrowArray& array, const Value& value) {
        return ok (ArrowArrayAppendBytes (
          &array, arrow_buffer_view (&value, sizeof (Value))));
      }

      static bool
      read (const ArrowArrayView& view, std::int64_t row, Value& value) {
        const ArrowBufferView bytes = ArrowArrayViewGetBytesUnsafe (&view, row);
        if (bytes.size_bytes != sizeof (Value))
          return false;
        std::memcpy (&value, bytes.data.data, sizeof (Value));
        return true;
      }
    };

    template <typename Value, ArrowScalar Rep>
    struct ColumnEncoding<Value, Rep> {
      static std::string name () {
        return "scalar";
      }

      static bool configure (ArrowSchema& schema) {
        return ok (ArrowSchemaSetType (&schema, arrow_scalar_type<Rep> ()));
      }

      static bool append (ArrowArray& array, const Value& value) {
        return append_scalar (array, representation (value));
      }

      static bool
      read (const ArrowArrayView& view, std::int64_t row, Value& value) {
        value = value_from_representation<Value> (scalar_at<Rep> (view, row));
        return true;
      }
    };

    template <typename Value, ArrowVector Rep>
    struct ColumnEncoding<Value, Rep> {
      using Component = typename Rep::value_type;
      static constexpr std::size_t extent = Rep::extent;

      static std::string name () {
        return std::format ("vector[{}]", extent);
      }

      static bool configure (ArrowSchema& schema) {
        return ok (ArrowSchemaSetTypeFixedSize (
                 &schema,
                 NANOARROW_TYPE_FIXED_SIZE_LIST,
                 static_cast<std::int32_t> (extent))) &&
               ok (ArrowSchemaSetType (schema.children[0],
                                       arrow_scalar_type<Component> ()));
      }

      static bool append (ArrowArray& array, const Value& value) {
        const Rep& components = representation (value);
        for (std::size_t component = 0; component < extent; ++component)
          if (!append_scalar (*array.children[0], components[component]))
            return false;
        return ok (ArrowArrayFinishElement (&array));
      }

      static bool
      read (const ArrowArrayView& view, std::int64_t row, Value& value) {
        Rep components {};
        for (std::size_t component = 0; component < extent; ++component) {
          const std::int64_t index = row * static_cast<std::int64_t> (extent) +
                                     static_cast<std::int64_t> (component);
          if (ArrowArrayViewIsNull (view.children[0], index))
            return false;
          components[component] =
            scalar_at<Component> (*view.children[0], index);
        }
        value = value_from_representation<Value> (std::move (components));
        return true;
      }
    };

    template <typename Value>
    Metadata column_metadata () {
      return { { std::string (kind_key), quantity_kind<Value> () },
               { std::string (unit_key), std::format ("{:P}", Value::unit) },
               { std::string (dimension_key),
                 std::format ("{:P}", Value::dimension) },
               { std::string (spec_key), quantity_spec_name<Value> () },
               { std::string (storage_key), ColumnEncoding<Value>::name () } };
    }

    template <typename Value>
    bool configure_column_schema (ArrowSchema& schema) {
      const std::string spec = quantity_spec_name<Value> ();
      return ColumnEncoding<Value>::configure (schema) &&
             ok (ArrowSchemaSetName (&schema, spec.c_str ())) &&
             set_metadata (schema, column_metadata<Value> ());
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

    // A stored column belongs to this quantity when it has the field this
    // quantity would write, down to its units.
    template <typename Value>
    bool column_schema_matches (const ArrowSchema& actual) {
      UniqueSchema expected;
      ArrowSchemaInit (expected.get ());
      return configure_column_schema<Value> (*expected.get ()) &&
             schema_shape_matches (actual, *expected.get ()) &&
             metadata_matches (actual, column_metadata<Value> ());
    }

    template <typename Value>
    bool
    read_value (const ArrowArrayView& view, std::int64_t row, Value& value) {
      return !ArrowArrayViewIsNull (&view, row) &&
             ColumnEncoding<Value>::read (view, row, value);
    }

    // Fold a check over the columns of a bundle, handing each one its value
    // type and position. Stops at the first column that says no.
    template <typename... Values, typename Check>
    bool every_column (Check&& check) {
      return [&]<std::size_t... Column> (std::index_sequence<Column...>) {
        return (check.template operator()<Values, Column> () && ...);
      }(std::index_sequence_for<Values...> {});
    }

    // ---- Domain identity ---------------------------------------------------
    // The domain writes opaque bytes only it can interpret; hex keeps schema
    // metadata printable for generic Arrow tooling.

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

    template <typename Domain>
    std::string domain_description (const Domain& domain) {
      std::ostringstream out (std::ios::binary);
      DomainStorage<Domain>::write (out, domain);
      return std::move (out).str ();
    }

    template <typename Domain>
    Metadata bundle_metadata (const Domain& domain) {
      return { { std::string (version_key), std::string (bundle_version) },
               { std::string (domain_type_key), domain_type_name<Domain> () },
               { std::string (domain_key),
                 hex_encode (domain_description (domain)) } };
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

    // ---- One bundle, one record batch --------------------------------------

    template <typename Domain, typename... Quantities>
    bool configure_bundle_schema (ArrowSchema& schema,
                                  const Bundle<Domain, Quantities...>& bundle) {
      ArrowSchemaInit (&schema);
      return ok (ArrowSchemaSetTypeStruct (
               &schema, static_cast<std::int64_t> (sizeof...(Quantities)))) &&
             every_column<Quantities...> (
               [&]<typename Value, std::size_t Column> () {
                 return configure_column_schema<Value> (
                   *schema.children[Column]);
               }) &&
             set_metadata (schema, bundle_metadata (bundle.domain ()));
    }

    template <typename Domain, typename... Quantities>
    bool build_bundle_array (ArrowArray& array,
                             const Bundle<Domain, Quantities...>& bundle,
                             ArrowError& error) {
      for (std::size_t row = 0; row < bundle.size (); ++row) {
        const bool appended = every_column<Quantities...> (
          [&]<typename Value, std::size_t Column> () {
            return ColumnEncoding<Value>::append (*array.children[Column],
                                                  get<Column> (bundle)[row]);
          });
        if (!appended || !ok (ArrowArrayFinishElement (&array)))
          return false;
      }
      return ok (ArrowArrayFinishBuilding (
        &array, NANOARROW_VALIDATION_LEVEL_FULL, &error));
    }

    // The IPC writer borrows the encoded buffer, which therefore belongs to
    // the caller and outlives this call.
    template <typename Domain, typename... Quantities>
    bool encode_bundle (UniqueBuffer& encoded,
                        const Bundle<Domain, Quantities...>& bundle) {
      ArrowError error;
      ArrowErrorInit (&error);

      UniqueSchema schema;
      UniqueArray array;
      UniqueArrayView view;
      if (!configure_bundle_schema (*schema.get (), bundle) ||
          !ok (
            ArrowArrayInitFromSchema (array.get (), schema.get (), &error)) ||
          !build_bundle_array (*array.get (), bundle, error) ||
          !ok (ArrowArrayViewInitFromSchema (
            view.get (), schema.get (), &error)) ||
          !ok (ArrowArrayViewSetArray (view.get (), array.get (), &error)))
        return false;

      UniqueOutputStream output;
      UniqueWriter writer;
      if (!ok (
            ArrowIpcOutputStreamInitBuffer (output.get (), encoded.get ())) ||
          !ok (ArrowIpcWriterInit (writer.get (), output.get ())))
        return false;

      return ok (ArrowIpcWriterWriteSchema (
               writer.get (), schema.get (), &error)) &&
             ok (ArrowIpcWriterWriteArrayView (
               writer.get (), view.get (), &error)) &&
             ok (ArrowIpcWriterWriteArrayView (writer.get (), nullptr, &error));
    }

    inline bool read_ipc_stream (std::istream& in, UniqueArrayStream& stream) {
      const std::string bytes ((std::istreambuf_iterator<char> (in)),
                               std::istreambuf_iterator<char> ());
      if (bytes.empty ())
        return false;

      UniqueBuffer input;
      if (!ok (ArrowBufferAppend (input.get (), bytes.data (), bytes.size ())))
        return false;

      UniqueInputStream ipc_input;
      return ok (ArrowIpcInputStreamInitBuffer (ipc_input.get (),
                                                input.get ())) &&
             ok (ArrowIpcArrayStreamReaderInit (
               stream.get (), ipc_input.get (), nullptr));
    }

    // A bundle is exactly one record batch, so a second one means this file
    // is telling some other story.
    inline bool read_only_batch (ArrowArrayStream& stream,
                                 UniqueArray& array,
                                 ArrowError& error) {
      if (!ok (ArrowArrayStreamGetNext (&stream, array.get (), &error)) ||
          !array->release)
        return false;

      UniqueArray end;
      return ok (ArrowArrayStreamGetNext (&stream, end.get (), &error)) &&
             !end->release;
    }

    inline bool view_batch (UniqueArrayView& view,
                            ArrowSchema& schema,
                            ArrowArray& array,
                            ArrowError& error) {
      return ok (ArrowArrayViewInitFromSchema (view.get (), &schema, &error)) &&
             ok (ArrowArrayViewSetArray (view.get (), &array, &error)) &&
             ok (ArrowArrayViewValidate (
               view.get (), NANOARROW_VALIDATION_LEVEL_FULL, &error));
    }
  }

  template <typename Domain, typename... Quantities>
    requires StorableDomain<Domain> && (StorableValue<Quantities> && ...)
  void write_bundle (std::ostream& out,
                     const Bundle<Domain, Quantities...>& bundle) {
    detail::UniqueBuffer encoded;
    if (!detail::encode_bundle (encoded, bundle)) {
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
      if (!detail::ok (
            ArrowArrayStreamGetSchema (stream.get (), schema.get (), &error)) ||
          !columns_match (*schema.get ()))
        return std::nullopt;

      std::optional<Domain> domain =
        detail::read_domain<Domain> (*schema.get ());
      if (!domain)
        return std::nullopt;

      detail::UniqueArray array;
      detail::UniqueArrayView view;
      if (!detail::read_only_batch (*stream.get (), array, error) ||
          array->length != static_cast<std::int64_t> (domain->size ()) ||
          !detail::view_batch (view, *schema.get (), *array.get (), error))
        return std::nullopt;

      bundle_type bundle (std::move (*domain));
      if (!fill_rows (bundle, *view.get ()))
        return std::nullopt;
      return bundle;
    }

  private:
    static bool columns_match (const ArrowSchema& schema) {
      return schema.n_children ==
               static_cast<std::int64_t> (sizeof...(Quantities)) &&
             detail::every_column<Quantities...> (
               [&]<typename Value, std::size_t Column> () {
                 return detail::column_schema_matches<Value> (
                   *schema.children[Column]);
               });
    }

    static bool fill_rows (bundle_type& bundle, const ArrowArrayView& view) {
      for (std::size_t row = 0; row < bundle.size (); ++row) {
        const bool values_read = detail::every_column<Quantities...> (
          [&]<typename Value, std::size_t Column> () {
            return detail::read_value (*view.children[Column],
                                       static_cast<std::int64_t> (row),
                                       get<Column> (bundle)[row]);
          });
        if (!values_read)
          return false;
      }
      return true;
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
