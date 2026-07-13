#pragma once

#include "atelier/matrix.hh"
#include "atelier/tree.hh"

#include <vector>

namespace atelier {
  enum class TreeEmbeddingKind { diagram, wind_bent };

  struct EmbeddedTreeBud {
    Matrix place_in_world;
    Real vigor;
    Real material_seed;
    TreeOrgan organ;
  };

  struct EmbeddedTreeBranch {
    simd_float3 start;
    simd_float3 end;
    simd_float3 start_normal;
    simd_float3 end_normal;
    Real strain;
    Real bend;
    Length radius;
    Real material_seed;
    Real xylem;
    Real phloem;
  };

  struct EmbeddedTree {
    Matrix world_to_clip;
    Point eye;
    std::vector<EmbeddedTreeBud> buds;
    std::vector<EmbeddedTreeBranch> branches;
  };

  [[nodiscard]] EmbeddedTree embed_tree (const Tree& tree,
                                         TreeEmbeddingKind kind,
                                         Duration elapsed,
                                         Real aspect_ratio);
}
