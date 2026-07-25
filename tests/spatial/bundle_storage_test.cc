#include <moppe/spatial/bundle_storage.hh>

#include <moppe/terrain/domain_storage.hh>
#include <moppe/terrain/terrain_quantities.hh>

#include <tests/test.hh>

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

using namespace moppe;

namespace {
  // A domain whose site count belongs to its identity, so a file written
  // over one of them can be recognized as foreign by another.
  struct StoredRing {
    using index_type = std::size_t;

    std::size_t sites = 3;

    std::size_t size () const {
      return sites;
    }
    std::size_t offset (index_type index) const {
      return index;
    }
    index_type index (std::size_t offset) const {
      return offset;
    }

    friend bool operator== (const StoredRing&, const StoredRing&) = default;
  };

  inline constexpr struct stored_displacement
      : quantity_spec<mp_units::isq::length, mp_units::is_kind> {
  } stored_displacement;
  inline constexpr struct stored_density
      : quantity_spec<mp_units::dimensionless> {
  } stored_density;

  using StoredDisplacement = quantity<stored_displacement[u::m], float>;
  using StoredDensity = quantity<stored_density[one], float>;
  using StoredRingBundle =
    spatial::Bundle<StoredRing, StoredDisplacement, StoredDensity>;
  using StoredNarrowBundle = spatial::Bundle<StoredRing, StoredDisplacement>;

  inline constexpr struct stored_direction
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } stored_direction;
  using StoredDirection = quantity<stored_direction[one], Vec3>;
  using StoredVectorBundle = spatial::Bundle<StoredRing, StoredDirection>;

  inline constexpr struct stored_duration
      : quantity_spec<mp_units::isq::duration, mp_units::is_kind> {
  } stored_duration;
  using StoredDuration = quantity<stored_duration[u::s], float>;
  using StoredWrongUnitsBundle =
    spatial::Bundle<StoredRing, StoredDuration, StoredDensity>;
}

namespace moppe::spatial {
  template <>
  struct DomainStorage<StoredRing> {
    static void write (std::ostream& out, const StoredRing& domain) {
      detail::write_scalar (out, static_cast<std::uint64_t> (domain.sites));
    }

    static std::optional<StoredRing> read (std::istream& in) {
      std::uint64_t sites = 0;
      if (!detail::read_scalar (in, sites) || sites == 0)
        return std::nullopt;
      return StoredRing { static_cast<std::size_t> (sites) };
    }
  };
}

namespace {
  StoredRingBundle written_ring () {
    StoredRingBundle bundle (StoredRing { 3 });
    auto& [displacement, density] = bundle;
    displacement[0] = 1.0f * stored_displacement[u::m];
    displacement[2] = 5.0f * stored_displacement[u::m];
    density[1] = 0.25f * stored_density[one];
    return bundle;
  }
}

MOPPE_TEST (a_bundle_round_trips_every_column_over_its_own_domain) {
  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  spatial::write_bundle (file, written_ring ());

  const std::optional loaded = spatial::read_bundle<StoredRingBundle> (file);
  MOPPE_CHECK (loaded.has_value ());
  MOPPE_CHECK (loaded->domain () == StoredRing { 3 });
  const auto& displacement = spatial::get<stored_displacement> (*loaded);
  const auto& density = spatial::get<stored_density> (*loaded);
  MOPPE_CHECK_NEAR (displacement[0].numerical_value_in (u::m), 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (displacement[2].numerical_value_in (u::m), 5.0f, 1e-6f);
  MOPPE_CHECK_NEAR (density[1].numerical_value_in (one), 0.25f, 1e-6f);
}

MOPPE_TEST (a_bundle_file_is_an_arrow_stream_with_quantity_metadata) {
  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  spatial::write_bundle (file, written_ring ());

  const std::string bytes = file.str ();
  MOPPE_CHECK (bytes.size () > 4);
  MOPPE_CHECK (static_cast<unsigned char> (bytes[0]) == 0xff);
  MOPPE_CHECK (static_cast<unsigned char> (bytes[1]) == 0xff);
  MOPPE_CHECK (static_cast<unsigned char> (bytes[2]) == 0xff);
  MOPPE_CHECK (static_cast<unsigned char> (bytes[3]) == 0xff);

  file.seekg (0);
  spatial::detail::UniqueArrayStream stream;
  MOPPE_CHECK (spatial::detail::read_ipc_stream (file, stream));
  ArrowError error;
  ArrowErrorInit (&error);
  spatial::detail::UniqueSchema schema;
  MOPPE_CHECK (ArrowArrayStreamGetSchema (
                 stream.get (), schema.get (), &error) == NANOARROW_OK);
  MOPPE_CHECK (std::string_view (schema->format) == "+s");
  MOPPE_CHECK (schema->n_children == 2);

  const ArrowSchema& displacement = *schema->children[0];
  MOPPE_CHECK (std::string_view (displacement.name) ==
               spatial::detail::quantity_spec_name<StoredDisplacement> ());
  MOPPE_CHECK (std::string_view (displacement.format) == "f");
  MOPPE_CHECK (spatial::detail::metadata_value (
                 displacement, spatial::detail::unit_key) == "m");
  MOPPE_CHECK (spatial::detail::metadata_value (
                 displacement, spatial::detail::dimension_key) == "L");
  MOPPE_CHECK (spatial::detail::metadata_value (
                 displacement, spatial::detail::kind_key) == "quantity");

  spatial::detail::UniqueArray batch;
  MOPPE_CHECK (ArrowArrayStreamGetNext (stream.get (), batch.get (), &error) ==
               NANOARROW_OK);
  spatial::detail::UniqueArrayView view;
  MOPPE_CHECK (ArrowArrayViewInitFromSchema (
                 view.get (), schema.get (), &error) == NANOARROW_OK);
  MOPPE_CHECK (ArrowArrayViewSetArray (view.get (), batch.get (), &error) ==
               NANOARROW_OK);
  MOPPE_CHECK_NEAR (
    ArrowArrayViewGetDoubleUnsafe (view->children[0], 2), 5.0, 1e-6);
}

MOPPE_TEST (a_vector_quantity_is_an_arrow_fixed_size_list) {
  StoredVectorBundle bundle (StoredRing { 3 });
  spatial::get<stored_direction> (bundle)[1] =
    StoredDirection (Vec3 { 1.0f, 2.0f, 3.0f }, StoredDirection::reference);

  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  spatial::write_bundle (file, bundle);
  const std::optional loaded = spatial::read_bundle<StoredVectorBundle> (file);

  MOPPE_CHECK (loaded.has_value ());
  const Vec3 value =
    spatial::get<stored_direction> (*loaded)[1].numerical_value_in (one);
  MOPPE_CHECK_NEAR (value[0], 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (value[1], 2.0f, 1e-6f);
  MOPPE_CHECK_NEAR (value[2], 3.0f, 1e-6f);
}

MOPPE_TEST (loading_keeps_the_callers_bundle_when_the_domain_differs) {
  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  spatial::write_bundle (file, written_ring ());

  StoredRingBundle wider (StoredRing { 5 });
  MOPPE_CHECK (!spatial::load_bundle (file, wider));
  MOPPE_CHECK (wider.size () == 5);
  MOPPE_CHECK_NEAR (
    spatial::get<stored_displacement> (wider)[0].numerical_value_in (u::m),
    0.0f,
    1e-6f);
}

MOPPE_TEST (a_file_from_another_column_shape_is_no_file_at_all) {
  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  spatial::write_bundle (file, written_ring ());

  StoredNarrowBundle narrow (StoredRing { 3 });
  MOPPE_CHECK (!spatial::load_bundle (file, narrow));
}

MOPPE_TEST (a_file_with_other_units_is_no_file_at_all) {
  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  spatial::write_bundle (file, written_ring ());

  StoredWrongUnitsBundle wrong_units (StoredRing { 3 });
  MOPPE_CHECK (!spatial::load_bundle (file, wrong_units));
}

MOPPE_TEST (an_empty_file_is_no_file_at_all) {
  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  StoredRingBundle bundle (StoredRing { 3 });
  MOPPE_CHECK (!spatial::load_bundle (file, bundle));
}

MOPPE_TEST (a_terrain_bundle_recovers_its_lattice_from_its_file) {
  using StoredElevationBundle =
    spatial::Bundle<terrain::TerrainDomain, terrain::SurfaceElevation>;
  const terrain::TerrainDomain domain (4, 3, 2.0f * u::m, 0.5f * u::m);
  StoredElevationBundle bundle (domain);
  spatial::get<terrain::surface_elevation> (bundle)[7] =
    terrain::surface_elevation_point (12.5f * u::m);

  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  spatial::write_bundle (file, bundle);
  const std::optional loaded =
    spatial::read_bundle<StoredElevationBundle> (file);

  MOPPE_CHECK (loaded.has_value ());
  MOPPE_CHECK (loaded->domain () == domain);
  MOPPE_CHECK_NEAR (terrain::surface_elevation_value (
                      spatial::get<terrain::surface_elevation> (*loaded)[7]),
                    12.5f,
                    1e-6f);
}
