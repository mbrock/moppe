#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace moppe::nhal::detail {
  // Pre-generated botanical tree archetype vertex, packed into three vec4
  // rows (48 bytes) for direct vertex pulling across all NHAL backends:
  //   pos_scale : (x, y, z, radial_mode) -- y in height fractions; x,z in
  //               trunk_radius units (radial_mode == 0) or crown_radius
  //               units (radial_mode == 1).
  //   norm_ao   : (nx, ny, nz, ao) -- local outward normal and baked 3D
  //               volumetric crown ambient occlusion [0.35..1.0].
  //   uv_meta   : (u, v, flutter, part) -- for bark (part < 1.5), u is
  //               circumferential arc and v is stem height fraction; for
  //               foliage (part >= 1.5), u is clump seed [0..1] and v is
  //               crown height [0..1]. flutter in [0..1], part: 0 trunk,
  //               1 branch, 2 foliage.
  struct TreeArchetypeVertex {
    std::array<float, 4> pos_scale;
    std::array<float, 4> norm_ao;
    std::array<float, 4> uv_meta;
  };
  static_assert (sizeof (TreeArchetypeVertex) == 48);

  struct TreeArchetypeSlice {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
  };

  struct TreeArchetypeLibrary {
    std::vector<TreeArchetypeVertex> vertices;
    std::vector<std::uint16_t> indices;
    // Indexed as (species * variants + variant) * tiers + tier, where tier 0
    // is coarsest (far & near-shadow) and tiers-1 is finest (hero close-up).
    std::vector<TreeArchetypeSlice> slices;
  };

  // Builds the deterministic library of botanical tree archetypes once at
  // startup for all five ecological species (Birch, Spruce, Pine, FellBirch,
  // Snag), `variants` distinct silhouettes each, across `tiers` coverage-
  // conserving LODs. Every triangle has consistent outward CCW winding for
  // hardware back-face culling and fits within 16-bit vertex indices.
  TreeArchetypeLibrary build_tree_archetypes (std::uint32_t species,
                                              std::uint32_t variants,
                                              std::uint32_t tiers);
}
