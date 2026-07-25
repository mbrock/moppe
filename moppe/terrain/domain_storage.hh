#ifndef MOPPE_TERRAIN_DOMAIN_STORAGE_HH
#define MOPPE_TERRAIN_DOMAIN_STORAGE_HH

#include <moppe/spatial/bundle_storage.hh>
#include <moppe/terrain/domain.hh>

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>

// The terrain lattice writes the four numbers that are its identity, so any
// bundle over it saves and loads generically.

namespace moppe::spatial {
  template <>
  struct DomainStorage<terrain::TerrainDomain> {
    static void write (std::ostream& out,
                       const terrain::TerrainDomain& domain) {
      detail::write_scalar (out, static_cast<std::uint32_t> (domain.width ()));
      detail::write_scalar (out, static_cast<std::uint32_t> (domain.height ()));
      detail::write_scalar (out, domain.spacing_x ().numerical_value_in (u::m));
      detail::write_scalar (out, domain.spacing_z ().numerical_value_in (u::m));
    }

    static std::optional<terrain::TerrainDomain> read (std::istream& in) {
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      float spacing_x = 0.0f;
      float spacing_z = 0.0f;
      if (!detail::read_scalar (in, width) ||
          !detail::read_scalar (in, height) ||
          !detail::read_scalar (in, spacing_x) ||
          !detail::read_scalar (in, spacing_z))
        return std::nullopt;
      if (width < 2 || height < 2 || !(spacing_x > 0.0f) || !(spacing_z > 0.0f))
        return std::nullopt;
      return terrain::TerrainDomain (
        width, height, spacing_x * u::m, spacing_z * u::m);
    }
  };
}

#endif
