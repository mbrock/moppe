#ifndef MOPPE_RENDER_TREE_ARCHETYPES_HH
#define MOPPE_RENDER_TREE_ARCHETYPES_HH

#include <moppe/gfx/math.hh>
#include <moppe/render/forest_kinds.hh>

#include <array>
#include <cstdint>
#include <vector>

namespace moppe::render {
  // Which structural tissue a vertex belongs to; packed into vertex metadata
  // for bark vs foliage shading and wind response.
  enum class TreePart : std::uint8_t { trunk = 0, branch = 1, foliage = 2 };

  // Which horizontal length scales a vertex's x and z coordinates at draw
  // time: the trunk's girth (matching the physics collider) or the crown's
  // spread.
  enum class RadialFrame : std::uint8_t { trunk_radius = 0, crown_radius = 1 };

  // Canonical proportions of a species: reference size for sculpting each
  // archetype, trunk girth ratio shared with physics trunk colliders, and
  // open-grown crown start height.
  struct TreeSpeciesProfile {
    meters_t reference_height;
    meters_t reference_crown_radius;
    proportion_t trunk_radius_ratio;
    proportion_t crown_base_ratio;
  };

  inline constexpr std::uint32_t forest_species_count = 5;

  TreeSpeciesProfile tree_species_profile (ForestSpecies species);

  // GPU storage-boundary vertex packed into three vec4 rows (48 bytes) for
  // direct vertex pulling across all NHAL backends:
  //   pos_scale : (x, y, z, radial_frame) -- y in height fractions; x,z in
  //               trunk_radius units (0) or crown_radius units (1).
  //   norm_ao   : (nx, ny, nz, ao) -- local outward normal and volumetric
  //               canopy ambient occlusion in [0.34..1.0].
  //   uv_meta   : (u, v, flutter, part) -- for bark (part < 1.5), u is
  //               circumferential arc and v is stem height fraction; for
  //               foliage (part >= 1.5), u is clump seed [0..1] and v is
  //               crown height [0..1].
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
    std::uint32_t variants = 0;
    std::uint32_t tiers = 0;
    // Indexed as (species * variants + variant) * tiers + tier, where tier 0
    // is coarsest (far & near-shadow) and tiers-1 is finest (hero close-up).
    std::vector<TreeArchetypeSlice> slices;

    TreeArchetypeSlice slice_for (ForestSpecies species,
                                  std::uint32_t variant,
                                  std::uint32_t tier) const {
      const std::size_t idx =
        (std::size_t (species) * variants + variant) * tiers + tier;
      return slices[idx];
    }
  };

  // Builds the deterministic, bit-identical library of botanical tree
  // archetypes once at startup for all five ForestSpecies, `variants`
  // silhouettes each, across `tiers` coverage-conserving LODs. Every triangle
  // has verified outward CCW winding and fits within 16-bit vertex indices.
  TreeArchetypeLibrary build_tree_archetypes (std::uint32_t variants,
                                              std::uint32_t tiers);
}

#endif
