#ifndef MOPPE_TERRAIN_GEOLOGICAL_HH
#define MOPPE_TERRAIN_GEOLOGICAL_HH

#include <moppe/terrain/field.hh>

#include <cstdint>
#include <optional>
#include <string_view>

namespace moppe::terrain {
  enum class GeologicalLayer {
    Combined,
    Continent,
    Plains,
    Mountains,
    MountainMask,
    WarpX,
    WarpY
  };

  struct GeologicalSeeds {
    std::uint32_t base;
    std::uint32_t ridge;
    std::uint32_t warp;
  };

  struct GeologicalFields {
    ScalarField warp_x;
    ScalarField warp_y;
    ScalarField warped_x;
    ScalarField warped_y;
    ScalarField continent;
    ScalarField plains;
    ScalarField mountains;
    ScalarField mountain_mask;
    ScalarField combined;
  };

  GeologicalSeeds derive_geological_seeds (std::uint32_t root_seed);
  GeologicalFields make_geological_fields (const GeologicalSeeds& seeds);
  ScalarField geological_layer (const GeologicalFields& fields,
				 GeologicalLayer layer);
  const char* geological_layer_name (GeologicalLayer layer);
  std::string_view geological_layer_id (GeologicalLayer layer);
  std::optional<GeologicalLayer> geological_layer_from_id
    (std::string_view id);
}

#endif
