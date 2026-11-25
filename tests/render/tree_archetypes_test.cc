#include <moppe/render/tree_archetypes.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {
  using namespace moppe;
  using render::ForestSpecies;
  using render::TreeArchetypeLibrary;
  using render::TreeArchetypeSlice;
  using render::TreeArchetypeVertex;

  constexpr std::array<ForestSpecies, render::forest_species_count>
    all_species { ForestSpecies::Broadleaf,
                  ForestSpecies::Conifer,
                  ForestSpecies::Pine,
                  ForestSpecies::FellBirch,
                  ForestSpecies::Snag };
}

MOPPE_TEST (tree_archetypes_are_bit_identical_across_calls) {
  const TreeArchetypeLibrary a = render::build_tree_archetypes (3, 3);
  const TreeArchetypeLibrary b = render::build_tree_archetypes (3, 3);
  MOPPE_CHECK (a.vertices.size () == b.vertices.size ());
  MOPPE_CHECK (a.indices == b.indices);
  MOPPE_CHECK (a.slices.size () ==
               std::size_t (render::forest_species_count) * 9);
  MOPPE_CHECK (
    std::memcmp (a.vertices.data (),
                 b.vertices.data (),
                 a.vertices.size () * sizeof (TreeArchetypeVertex)) == 0);
}

MOPPE_TEST (tree_archetypes_fit_uint16_with_valid_normals_and_bounds) {
  const TreeArchetypeLibrary lib = render::build_tree_archetypes (3, 3);
  MOPPE_CHECK (!lib.vertices.empty ());
  MOPPE_CHECK (lib.vertices.size () < 65535u);

  for (const TreeArchetypeVertex& v : lib.vertices) {
    for (int i = 0; i < 4; ++i) {
      MOPPE_CHECK (std::isfinite (v.pos_scale[i]));
      MOPPE_CHECK (std::isfinite (v.norm_ao[i]));
      MOPPE_CHECK (std::isfinite (v.uv_meta[i]));
    }
    const magnitude_t normal_len =
      std::sqrt (v.norm_ao[0] * v.norm_ao[0] + v.norm_ao[1] * v.norm_ao[1] +
                 v.norm_ao[2] * v.norm_ao[2]) *
      one;
    MOPPE_CHECK_NEAR (scalar_value (normal_len), 1.0f, 0.01f);
    const proportion_t ao = v.norm_ao[3] * proportion[one];
    const proportion_t flutter = v.uv_meta[2] * proportion[one];
    MOPPE_CHECK (ao >= 0.34f * proportion[one] &&
                 ao <= 1.001f * proportion[one]);
    MOPPE_CHECK (flutter >= 0.0f * proportion[one] &&
                 flutter <= 1.001f * proportion[one]);
  }

  for (const ForestSpecies sp : all_species) {
    const render::TreeSpeciesProfile profile =
      render::tree_species_profile (sp);
    MOPPE_CHECK (profile.reference_height > 3.0f * u::m);
    MOPPE_CHECK (profile.reference_crown_radius > 1.5f * u::m);
    MOPPE_CHECK (profile.trunk_radius_ratio > 0.005f * proportion[one]);

    for (std::uint32_t var = 0; var < 3; ++var) {
      std::uint32_t prev_count = 0;
      for (std::uint32_t tier = 0; tier < 3; ++tier) {
        const TreeArchetypeSlice slice = lib.slice_for (sp, var, tier);
        MOPPE_CHECK (slice.index_count > 0);
        MOPPE_CHECK (slice.index_count % 3 == 0);
        // Finer LOD tiers strictly add geometric detail.
        MOPPE_CHECK (slice.index_count > prev_count);
        prev_count = slice.index_count;

        meters_t y_min = 100.0f * u::m;
        meters_t y_max = -100.0f * u::m;
        for (std::uint32_t k = 0; k < slice.index_count; k += 3) {
          const std::uint16_t ia = lib.indices[slice.first_index + k];
          const std::uint16_t ib = lib.indices[slice.first_index + k + 1];
          const std::uint16_t ic = lib.indices[slice.first_index + k + 2];
          MOPPE_CHECK (ia < lib.vertices.size ());
          MOPPE_CHECK (ib < lib.vertices.size ());
          MOPPE_CHECK (ic < lib.vertices.size ());
          MOPPE_CHECK (ia != ib && ib != ic && ia != ic);
          for (std::uint16_t id : { ia, ib, ic }) {
            const meters_t y =
              profile.reference_height * lib.vertices[id].pos_scale[1];
            y_min = std::min (y_min, y);
            y_max = std::max (y_max, y);
          }
        }
        // Every tree roots below ground and reaches near its nominal height.
        MOPPE_CHECK (y_min < 0.0f * u::m);
        MOPPE_CHECK (y_max > profile.reference_height * 0.85f);
        MOPPE_CHECK (y_max < profile.reference_height * 1.38f);
      }
    }
  }
}

MOPPE_TEST (tree_archetypes_compute_volumetric_canopy_occlusion) {
  const TreeArchetypeLibrary lib = render::build_tree_archetypes (3, 3);
  for (const ForestSpecies sp : { ForestSpecies::Broadleaf,
                                  ForestSpecies::Conifer,
                                  ForestSpecies::Pine,
                                  ForestSpecies::FellBirch }) {
    const TreeArchetypeSlice hero = lib.slice_for (sp, 0, 2);
    proportion_t min_ao = 1.0f * proportion[one];
    proportion_t max_ao = 0.0f * proportion[one];
    for (std::uint32_t k = 0; k < hero.index_count; ++k) {
      const TreeArchetypeVertex& v =
        lib.vertices[lib.indices[hero.first_index + k]];
      if (v.uv_meta[3] > 1.5f) {
        const proportion_t ao = v.norm_ao[3] * proportion[one];
        min_ao = std::min (min_ao, ao);
        max_ao = std::max (max_ao, ao);
      }
    }
    // Crown interiors carry genuine shade while outer tips stay sunlit.
    MOPPE_CHECK (min_ao < 0.72f * proportion[one]);
    MOPPE_CHECK (max_ao > 0.86f * proportion[one]);
    MOPPE_CHECK (max_ao - min_ao > 0.22f * proportion[one]);
  }
}
