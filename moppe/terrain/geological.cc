#include <moppe/terrain/geological.hh>

#include <random>

namespace moppe::terrain {
  GeologicalSeeds derive_geological_seeds (std::uint32_t root_seed) {
    std::mt19937 rng (root_seed);
    return {
      .base = rng (),
      .ridge = rng (),
      .warp = rng ()
    };
  }

  GeologicalFields make_geological_fields
    (const GeologicalSeeds& seeds) {
    const ScalarField u = coordinate_x ();
    const ScalarField v = coordinate_y ();

    const ScalarField warp_x = fbm_noise
      (seeds.warp, u * 3.0f + 11.3f, v * 3.0f + 7.7f,
       4, 2.0f, 0.5f);
    const ScalarField warp_y = fbm_noise
      (seeds.warp, u * 3.0f + 91.1f, v * 3.0f + 33.9f,
       4, 2.0f, 0.5f);
    const ScalarField warped_x = multiply_add
      (constant (0.15f), warp_x, u);
    const ScalarField warped_y = multiply_add
      (constant (0.15f), warp_y, v);

    const ScalarField continent = multiply_add
      (fbm_noise
      (seeds.base, warped_x * 2.5f, warped_y * 2.5f,
       4, 2.0f, 0.5f), constant (0.5f), constant (0.5f));
    const ScalarField plains = multiply_add
      (fbm_noise
      (seeds.base, warped_x * 12.0f, warped_y * 12.0f,
       4, 2.0f, 0.5f), constant (0.5f), constant (0.5f));
    const ScalarField mountains = ridged_noise
      (seeds.ridge, warped_x * 6.0f, warped_y * 6.0f,
       6, 2.05f, 0.55f);
    const ScalarField mountain_mask = smoothstep
      (0.45f, 0.75f, continent);

    const ScalarField lowland = plains * 0.12f
      * (1.0f - mountain_mask);
    const ScalarField combined = multiply_add
      (mountains * 0.65f, mountain_mask,
       multiply_add (continent, constant (0.55f), lowland));

    return {
      .warp_x = warp_x,
      .warp_y = warp_y,
      .warped_x = warped_x,
      .warped_y = warped_y,
      .continent = continent,
      .plains = plains,
      .mountains = mountains,
      .mountain_mask = mountain_mask,
      .combined = combined
    };
  }

  ScalarField geological_layer (const GeologicalFields& fields,
				 GeologicalLayer layer) {
    switch (layer) {
    case GeologicalLayer::Combined:     return fields.combined;
    case GeologicalLayer::Continent:    return fields.continent;
    case GeologicalLayer::Plains:       return fields.plains;
    case GeologicalLayer::Mountains:    return fields.mountains;
    case GeologicalLayer::MountainMask: return fields.mountain_mask;
    case GeologicalLayer::WarpX:        return fields.warp_x;
    case GeologicalLayer::WarpY:        return fields.warp_y;
    }
    return fields.combined;
  }

  const char* geological_layer_name (GeologicalLayer layer) {
    switch (layer) {
    case GeologicalLayer::Combined:     return "combined terrain";
    case GeologicalLayer::Continent:    return "continent field";
    case GeologicalLayer::Plains:       return "plains detail";
    case GeologicalLayer::Mountains:    return "ridged mountains";
    case GeologicalLayer::MountainMask: return "mountain mask";
    case GeologicalLayer::WarpX:        return "domain warp X";
    case GeologicalLayer::WarpY:        return "domain warp Y";
    }
    return "unknown";
  }

  std::string_view geological_layer_id (GeologicalLayer layer) {
    switch (layer) {
    case GeologicalLayer::Combined:     return "combined";
    case GeologicalLayer::Continent:    return "continent";
    case GeologicalLayer::Plains:       return "plains";
    case GeologicalLayer::Mountains:    return "mountains";
    case GeologicalLayer::MountainMask: return "mask";
    case GeologicalLayer::WarpX:        return "warp-x";
    case GeologicalLayer::WarpY:        return "warp-y";
    }
    return "unknown";
  }

  std::optional<GeologicalLayer> geological_layer_from_id
    (std::string_view id) {
    constexpr GeologicalLayer layers[] = {
      GeologicalLayer::Combined,
      GeologicalLayer::Continent,
      GeologicalLayer::Plains,
      GeologicalLayer::Mountains,
      GeologicalLayer::MountainMask,
      GeologicalLayer::WarpX,
      GeologicalLayer::WarpY
    };
    for (GeologicalLayer layer : layers)
      if (geological_layer_id (layer) == id)
	return layer;
    return std::nullopt;
  }
}
