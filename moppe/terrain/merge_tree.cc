#include <moppe/terrain/merge_tree.hh>

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace moppe::terrain {
  namespace {
    struct MergeOffset {
      int x;
      int y;
    };

    constexpr std::array<MergeOffset, 8> merge_neighbors { { { -1, -1 },
                                                             { 0, -1 },
                                                             { 1, -1 },
                                                             { -1, 0 },
                                                             { 1, 0 },
                                                             { -1, 1 },
                                                             { 0, 1 },
                                                             { 1, 1 } } };

    std::size_t merge_wrapped (int value, std::size_t period) {
      const int n = static_cast<int> (period);
      const int result = value % n;
      return static_cast<std::size_t> (result < 0 ? result + n : result);
    }

    // Path-halving union-find over cell indices; the component root
    // carries the id of its current merge-tree node.
    std::uint32_t find_root (std::vector<std::uint32_t>& parent,
                             std::uint32_t cell) {
      while (parent[cell] != cell) {
        parent[cell] = parent[parent[cell]];
        cell = parent[cell];
      }
      return cell;
    }
  }

  MergeTree build_merge_tree (const TerrainView& terrain) {
    const TerrainGrid& grid = terrain.grid ();
    const std::size_t width = grid.unique_width ();
    const std::size_t height = grid.unique_height ();
    const std::size_t count = width * height;
    const bool periodic = grid.topology == Topology::Torus;

    // Sort unique cells by (height, index): the same deterministic
    // order the drainage analyses use for tie-breaking.
    std::vector<std::uint32_t> order (count);
    std::iota (order.begin (), order.end (), 0u);
    std::sort (order.begin (),
               order.end (),
               [&terrain, width] (std::uint32_t a, std::uint32_t b) {
                 const float za = terrain.at (a % width, a / width);
                 const float zb = terrain.at (b % width, b / width);
                 if (za != zb)
                   return za < zb;
                 return a < b;
               });

    // Rank of each cell in the sweep; a neighbor is "already flooded"
    // exactly when its rank is smaller.
    std::vector<std::uint32_t> rank (count);
    for (std::uint32_t position = 0; position < count; ++position)
      rank[order[position]] = position;

    MergeTree tree;
    tree.source_grid = grid;
    tree.cell_node.assign (count, MergeTreeNode::no_node);
    tree.nodes.reserve (count / 16);

    std::vector<std::uint32_t> uf_parent (count);
    std::iota (uf_parent.begin (), uf_parent.end (), 0u);
    std::vector<std::uint32_t> root_node (count, MergeTreeNode::no_node);
    std::array<std::uint32_t, 8> joined {};

    for (std::uint32_t position = 0; position < count; ++position) {
      const std::uint32_t cell = order[position];
      const std::size_t x = cell % width;
      const std::size_t y = cell / width;
      const float z = terrain.at (x, y);

      std::size_t joined_count = 0;
      for (const MergeOffset offset : merge_neighbors) {
        const int raw_x = static_cast<int> (x) + offset.x;
        const int raw_y = static_cast<int> (y) + offset.y;
        if (!periodic &&
            (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<int> (width) ||
             raw_y >= static_cast<int> (height)))
          continue;
        const std::size_t nx = periodic ? merge_wrapped (raw_x, width)
                                        : static_cast<std::size_t> (raw_x);
        const std::size_t ny = periodic ? merge_wrapped (raw_y, height)
                                        : static_cast<std::size_t> (raw_y);
        const std::uint32_t neighbor =
          static_cast<std::uint32_t> (ny * width + nx);
        if (rank[neighbor] > position)
          continue;
        const std::uint32_t root = find_root (uf_parent, neighbor);
        bool seen = false;
        for (std::size_t i = 0; i < joined_count; ++i)
          seen = seen || joined[i] == root;
        if (!seen)
          joined[joined_count++] = root;
      }

      if (joined_count == 0) {
        // A local minimum: a leaf is born.
        const std::uint32_t node =
          static_cast<std::uint32_t> (tree.nodes.size ());
        tree.nodes.push_back (
          { .birth = z, .origin = CellIndex { cell }, .child_count = 0 });
        root_node[cell] = node;
        tree.cell_node[cell] = node;
        continue;
      }

      if (joined_count == 1) {
        // The cell joins an existing component.
        tree.cell_node[cell] = root_node[joined[0]];
        uf_parent[cell] = joined[0];
        continue;
      }

      // A saddle: several components merge into a new node.
      const std::uint32_t node =
        static_cast<std::uint32_t> (tree.nodes.size ());
      tree.nodes.push_back (
        { .birth = z,
          .origin = CellIndex { cell },
          .child_count = static_cast<std::uint32_t> (joined_count) });
      std::uint32_t merged = joined[0];
      for (std::size_t i = 0; i < joined_count; ++i) {
        tree.nodes[root_node[joined[i]]].parent = node;
        if (i > 0)
          uf_parent[joined[i]] = merged;
      }
      uf_parent[cell] = merged;
      root_node[merged] = node;
      tree.cell_node[cell] = node;
    }

    return tree;
  }

  MergeTreeFlood flood_from_merge_tree (const MergeTree& tree,
                                        const TerrainView& terrain,
                                        float sea_level) {
    if (!std::isfinite (sea_level))
      throw std::invalid_argument ("merge-tree sea level must be finite");
    const std::size_t width = tree.width ();
    const std::size_t height = tree.height ();
    const std::size_t count = width * height;
    const std::size_t node_count = tree.nodes.size ();
    constexpr std::uint32_t no_node = MergeTreeNode::no_node;

    // Components alive at sea level: for every node born at or below
    // the surface, the alive ancestor is the highest ancestor still
    // born at or below it.  Parents are stored after children, so one
    // reverse pass resolves every chain.
    std::vector<std::uint32_t> alive (node_count, no_node);
    for (std::size_t i = node_count; i-- > 0;) {
      const MergeTreeNode& node = tree.nodes[i];
      if (node.birth > sea_level)
        continue;
      const std::uint32_t parent = node.parent;
      alive[i] = (parent != no_node && tree.nodes[parent].birth <= sea_level)
                   ? alive[parent]
                   : static_cast<std::uint32_t> (i);
    }

    // The ocean is the largest submerged component at sea level; equal
    // sizes keep the component holding the smallest cell index, the
    // same deterministic choice the priority flood makes by scan order.
    std::vector<std::uint32_t> component_cells (node_count, 0);
    std::vector<std::uint32_t> component_first (
      node_count, std::numeric_limits<std::uint32_t>::max ());
    bool any_submerged = false;
    for (std::uint32_t cell = 0; cell < count; ++cell) {
      if (terrain.at (cell % width, cell / width) > sea_level)
        continue;
      any_submerged = true;
      const std::uint32_t component = alive[tree.cell_node[cell]];
      ++component_cells[component];
      component_first[component] = std::min (component_first[component], cell);
    }

    CellIndex root_cell = no_cell;
    float base_level = sea_level;
    if (any_submerged) {
      std::uint32_t best = no_node;
      for (std::uint32_t node = 0; node < node_count; ++node) {
        if (component_cells[node] == 0)
          continue;
        if (best == no_node || component_cells[node] > component_cells[best] ||
            (component_cells[node] == component_cells[best] &&
             component_first[node] < component_first[best]))
          best = node;
      }
      root_cell = CellIndex { component_first[best] };
    } else {
      // All-land world: root the surface at the deterministic global
      // minimum (first cell in (height, index) order).
      std::uint32_t minimum_cell = 0;
      float minimum = terrain.at (0, 0);
      for (std::uint32_t cell = 1; cell < count; ++cell) {
        const float z = terrain.at (cell % width, cell / width);
        if (z < minimum) {
          minimum = z;
          minimum_cell = cell;
        }
      }
      root_cell = CellIndex { minimum_cell };
      base_level = minimum;
    }

    // Join heights against the root cell's leaf-to-root chain: a cell
    // inserted into a marked node floods to its own height; any other
    // cell floods to the birth (saddle) height of the lowest marked
    // ancestor of its node.  Everything clamps to the base level.
    std::vector<std::uint8_t> marked (node_count, 0);
    for (std::uint32_t node = tree.cell_node[root_cell.value]; node != no_node;
         node = tree.nodes[node].parent)
      marked[node] = 1;
    constexpr float pending = -std::numeric_limits<float>::infinity ();
    std::vector<float> join (node_count, pending);
    for (std::size_t i = node_count; i-- > 0;) {
      if (marked[i])
        continue;
      const std::uint32_t parent = tree.nodes[i].parent;
      if (parent == no_node)
        join[i] = tree.nodes[i].birth;
      else
        join[i] = marked[parent] ? tree.nodes[parent].birth : join[parent];
    }

    std::vector<float> water (count);
    std::vector<std::uint8_t> ocean (count, 0);
    for (std::uint32_t cell = 0; cell < count; ++cell) {
      const float z = terrain.at (cell % width, cell / width);
      const std::uint32_t node = tree.cell_node[cell];
      const float join_height = marked[node] ? z : join[node];
      water[cell] = std::max (join_height, base_level);
      ocean[cell] = any_submerged && join_height <= sea_level ? 1 : 0;
    }

    const TerrainGrid& grid = tree.source_grid;
    const FieldSamplingGrid2D domain {
      .width = width,
      .height = height,
      .max_x = grid.spacing_x_m () * static_cast<float> (width),
      .max_y = grid.spacing_y_m () * static_cast<float> (height)
    };
    return { .has_ocean = any_submerged,
             .root_cell = root_cell,
             .water_level = ScalarRaster (domain, std::move (water)),
             .ocean = std::move (ocean) };
  }
}
