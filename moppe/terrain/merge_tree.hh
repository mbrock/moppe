#ifndef MOPPE_TERRAIN_MERGE_TREE_HH
#define MOPPE_TERRAIN_MERGE_TREE_HH

#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/types.hh>

#include <cstdint>
#include <vector>

namespace moppe::terrain {
  // The merge tree of the heightfield: how connected components of the
  // sublevel sets are born at minima and join at saddles as the water
  // level rises.  One deterministic O(n log n) precomputation answers
  // every standing-water question -- floods, lakes, spills, sea-level
  // queries -- as views, replacing per-query priority floods.
  //
  // Nodes are stored in creation order, so every parent appears after
  // all of its children; a single forward or reverse pass visits the
  // tree bottom-up or top-down.  Ties in height break by cell index,
  // preserving determinism-by-construction.
  struct MergeTreeNode {
    static constexpr std::uint32_t no_node = 0xffffffffu;

    // Leaves are born at their minimum's height; merge nodes at the
    // saddle height where two or more components join.
    float birth;
    // The minimum cell for a leaf; the saddle cell for a merge node.
    CellIndex origin;
    std::uint32_t parent = no_node;
    // Number of children (0 for a leaf); children are recovered via
    // parent pointers when needed.
    std::uint32_t child_count = 0;
  };

  struct MergeTree {
    TerrainGrid source_grid;
    std::vector<MergeTreeNode> nodes;
    // The node each unique cell was inserted into during the upward
    // sweep; the cell's component at any level L >= height(cell) is an
    // ancestor of this node.
    std::vector<std::uint32_t> cell_node;

    std::size_t width () const noexcept {
      return source_grid.unique_width ();
    }
    std::size_t height () const noexcept {
      return source_grid.unique_height ();
    }
  };

  MergeTree build_merge_tree (const TerrainView& terrain);

  // The minimax water surface at sea level: reproduces
  // analyze_standing_water's water_level raster (bit-exactly), the
  // ocean component, and the deterministic endorheic fallback, as a
  // pair of O(n) passes over the precomputed tree.
  struct MergeTreeFlood {
    bool has_ocean;
    // The representative cell the surface is rooted at: a cell of the
    // largest below-sea component, or the global minimum on an
    // all-land world.
    CellIndex root_cell;
    ScalarRaster water_level;
    std::vector<std::uint8_t> ocean;
  };

  MergeTreeFlood flood_from_merge_tree (const MergeTree& tree,
                                        const TerrainView& terrain,
                                        float sea_level);
}

#endif
