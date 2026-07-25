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

MOPPE_TEST (a_bundle_file_describes_each_quantity_before_its_binary_data) {
  std::stringstream file (std::ios::in | std::ios::out | std::ios::binary);
  spatial::write_bundle (file, written_ring ());

  std::string line;
  std::getline (file, line);
  MOPPE_CHECK (line == "MOPBNDL2");
  std::getline (file, line);
  MOPPE_CHECK (line == "columns=2");
  std::getline (file, line);
  MOPPE_CHECK (line == "quantity kind=quantity bytes=4 unit=[m] dimension=[L]");
  std::getline (file, line);
  MOPPE_CHECK (line == "quantity kind=quantity bytes=4 unit=[] dimension=[1]");
  std::getline (file, line);
  MOPPE_CHECK (line == "sites=3");
  std::getline (file, line);
  MOPPE_CHECK (line == "data");
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
