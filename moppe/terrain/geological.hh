#ifndef MOPPE_TERRAIN_GEOLOGICAL_HH
#define MOPPE_TERRAIN_GEOLOGICAL_HH

#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/domain.hh>

#include <functional>

namespace moppe::terrain {
  inline constexpr struct continent_shape
      : quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } continent_shape;
  inline constexpr struct uplift_weight
      : quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } uplift_weight;

  using ContinentShape = quantity<continent_shape[one], float>;
  using UpliftWeight = quantity<uplift_weight[one], float>;
  using GeologicalSections =
    spatial::Bundle<TerrainDomain, ContinentShape, UpliftWeight>;
  using GeologicalProgress =
    std::function<void (std::size_t completed_rows, std::size_t total_rows)>;

  GeologicalSections generate_geology (TerrainDomain domain,
                                       Seed seed,
                                       const GeologicalProgress& progress = {});
}

#endif
