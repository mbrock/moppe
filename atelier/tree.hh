#pragma once

#include "atelier/bundle.hh"
#include "atelier/space.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

// A tree is an oriented one-dimensional complex.  The topology remembers
// lineage and incidence; typed bundles carry the organism's intrinsic state.
// Neither layer says where the tree currently happens to be in world space.

namespace atelier {
  using TreeVertexId = std::size_t;
  using TreeEdgeId = std::size_t;
  inline constexpr TreeEdgeId no_tree_edge = static_cast<TreeEdgeId> (-1);

  struct TreeVertex {
    std::size_t generation;
    std::uint64_t lineage;
  };

  enum class TreeOrgan { shoot, root };

  struct TreeEdge {
    TreeVertexId parent;
    TreeVertexId child;
    std::size_t branch_order;
    bool continues_axis;
    TreeOrgan organ = TreeOrgan::shoot;
  };

  class DirectedTreeTopology {
  public:
    DirectedTreeTopology (std::vector<TreeVertex> vertices,
                          std::vector<TreeEdge> edges);

    [[nodiscard]] std::size_t vertex_count () const noexcept;
    [[nodiscard]] std::size_t edge_count () const noexcept;
    [[nodiscard]] const TreeVertex& vertex (TreeVertexId id) const;
    [[nodiscard]] const TreeEdge& edge (TreeEdgeId id) const;
    [[nodiscard]] TreeEdgeId parent_edge (TreeVertexId vertex) const;
    [[nodiscard]] std::span<const TreeEdgeId>
    child_edges (TreeVertexId vertex) const;
    [[nodiscard]] bool is_tip (TreeVertexId vertex) const;
    [[nodiscard]] bool is_valid () const;

  private:
    std::vector<TreeVertex> m_vertices;
    std::vector<TreeEdge> m_edges;
    std::vector<TreeEdgeId> m_parent_edges;
    std::vector<std::vector<TreeEdgeId>> m_child_edges;
  };

  class TreeVertexDomain {
  public:
    using index_type = TreeVertexId;

    explicit TreeVertexDomain (
      std::shared_ptr<const DirectedTreeTopology> topology);

    [[nodiscard]] std::size_t size () const noexcept;
    [[nodiscard]] constexpr std::size_t offset (TreeVertexId id) const {
      return id;
    }
    [[nodiscard]] constexpr TreeVertexId index (std::size_t offset) const {
      return offset;
    }
    [[nodiscard]] const DirectedTreeTopology& topology () const noexcept;

    template <typename Visitor>
    void visit_neighbourhood (TreeVertexId vertex, Visitor&& visitor) const {
      const DirectedTreeTopology& tree = topology ();
      const TreeEdgeId parent = tree.parent_edge (vertex);
      const std::size_t degree =
        tree.child_edges (vertex).size () + (parent == no_tree_edge ? 0 : 1);
      if (degree == 0)
        return;
      const Real influence = 1.0f / Real (degree);
      if (parent != no_tree_edge)
        visitor (tree.edge (parent).parent, influence);
      for (TreeEdgeId edge : tree.child_edges (vertex))
        visitor (tree.edge (edge).child, influence);
    }

  private:
    std::shared_ptr<const DirectedTreeTopology> m_topology;
  };

  class TreeEdgeDomain {
  public:
    using index_type = TreeEdgeId;

    explicit TreeEdgeDomain (
      std::shared_ptr<const DirectedTreeTopology> topology);

    [[nodiscard]] std::size_t size () const noexcept;
    [[nodiscard]] constexpr std::size_t offset (TreeEdgeId id) const {
      return id;
    }
    [[nodiscard]] constexpr TreeEdgeId index (std::size_t offset) const {
      return offset;
    }
    [[nodiscard]] const DirectedTreeTopology& topology () const noexcept;

  private:
    std::shared_ptr<const DirectedTreeTopology> m_topology;
  };

  enum class TreeFlowDirection { toward_root, away_from_root };

  // One accumulation law serves both a drainage in-tree and a botanical
  // out-tree.  Direction selects whether child values gather into parents or
  // parent values propagate into children; combine supplies the algebra.
  template <typename Value, typename Combine>
  [[nodiscard]] std::vector<Value>
  accumulate_along_tree (const DirectedTreeTopology& tree,
                         std::vector<Value> values,
                         TreeFlowDirection direction,
                         Combine combine) {
    if (values.size () != tree.vertex_count ())
      throw std::invalid_argument (
        "Tree accumulation needs one value per vertex");
    if (direction == TreeFlowDirection::toward_root) {
      for (TreeVertexId vertex = tree.vertex_count (); vertex-- > 1;) {
        const TreeEdge& edge = tree.edge (tree.parent_edge (vertex));
        values[edge.parent] = combine (values[edge.parent], values[vertex]);
      }
    } else {
      for (TreeVertexId vertex = 1; vertex < tree.vertex_count (); ++vertex) {
        const TreeEdge& edge = tree.edge (tree.parent_edge (vertex));
        values[vertex] = combine (values[vertex], values[edge.parent]);
      }
    }
    return values;
  }

  inline constexpr struct water_potential
      : quantity_spec<mp_units::dimensionless> {
  } water_potential;
  inline constexpr struct sugar_potential
      : quantity_spec<mp_units::dimensionless> {
  } sugar_potential;
  inline constexpr struct bud_vigor : quantity_spec<mp_units::dimensionless> {
  } bud_vigor;
  inline constexpr struct branch_rest_length
      : quantity_spec<isq::length, mp_units::is_kind> {
  } branch_rest_length;
  inline constexpr struct branch_radius
      : quantity_spec<isq::length, mp_units::is_kind> {
  } branch_radius;
  inline constexpr struct branch_flexibility
      : quantity_spec<mp_units::dimensionless> {
  } branch_flexibility;
  inline constexpr auto xylem_flux = water_potential / isq::duration;
  inline constexpr auto phloem_flux = sugar_potential / isq::duration;

  using WaterPotential = quantity<water_potential[mp_units::one], Real>;
  using SugarPotential = quantity<sugar_potential[mp_units::one], Real>;
  using BudVigor = quantity<bud_vigor[mp_units::one], Real>;
  using BranchRestLength = quantity<branch_rest_length[si::metre], Real>;
  using BranchRadius = quantity<branch_radius[si::metre], Real>;
  using BranchFlexibility = quantity<branch_flexibility[mp_units::one], Real>;
  using XylemFlux = quantity<xylem_flux[mp_units::one / si::second], Real>;
  using PhloemFlux = quantity<phloem_flux[mp_units::one / si::second], Real>;

  struct TreeEdgeForm {
    Angle azimuth;
    Angle elevation;
    Real material_seed;
  };

  class Tree {
  public:
    using VertexState =
      Bundle<TreeVertexDomain, WaterPotential, SugarPotential, BudVigor>;
    using EdgeState = Bundle<TreeEdgeDomain,
                             BranchRestLength,
                             BranchRadius,
                             BranchFlexibility,
                             XylemFlux,
                             PhloemFlux>;

    Tree ();

    [[nodiscard]] const DirectedTreeTopology& topology () const noexcept;
    [[nodiscard]] const VertexState& vertices () const noexcept;
    [[nodiscard]] const EdgeState& edges () const noexcept;
    [[nodiscard]] const TreeEdgeForm& form (TreeEdgeId edge) const;
    [[nodiscard]] std::size_t tip_count () const;

  private:
    Tree (std::tuple<std::shared_ptr<const DirectedTreeTopology>,
                     std::vector<TreeEdgeForm>> construction);

    std::shared_ptr<const DirectedTreeTopology> m_topology;
    VertexState m_vertices;
    EdgeState m_edges;
    std::vector<TreeEdgeForm> m_forms;
  };
}
