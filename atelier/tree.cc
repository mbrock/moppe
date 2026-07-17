#include "atelier/tree.hh"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace atelier {
  using moppe::spatial::get;
  using namespace si::unit_symbols;

  DirectedTreeTopology::DirectedTreeTopology (std::vector<TreeVertex> vertices,
                                              std::vector<TreeEdge> edges)
      : m_vertices (std::move (vertices)), m_edges (std::move (edges)),
        m_parent_edges (m_vertices.size (), no_tree_edge),
        m_child_edges (m_vertices.size ()) {
    for (TreeEdgeId id = 0; id < m_edges.size (); ++id) {
      const TreeEdge& edge = m_edges[id];
      if (edge.parent >= m_vertices.size () || edge.child >= m_vertices.size ())
        throw std::invalid_argument ("Tree edge names a missing vertex");
      if (m_parent_edges[edge.child] != no_tree_edge)
        throw std::invalid_argument ("Tree vertex has two parents");
      m_parent_edges[edge.child] = id;
      m_child_edges[edge.parent].push_back (id);
    }
    if (!is_valid ())
      throw std::invalid_argument ("Directed tree topology is inconsistent");
  }

  std::size_t DirectedTreeTopology::vertex_count () const noexcept {
    return m_vertices.size ();
  }

  std::size_t DirectedTreeTopology::edge_count () const noexcept {
    return m_edges.size ();
  }

  const TreeVertex& DirectedTreeTopology::vertex (TreeVertexId id) const {
    return m_vertices.at (id);
  }

  const TreeEdge& DirectedTreeTopology::edge (TreeEdgeId id) const {
    return m_edges.at (id);
  }

  TreeEdgeId DirectedTreeTopology::parent_edge (TreeVertexId vertex) const {
    return m_parent_edges.at (vertex);
  }

  std::span<const TreeEdgeId>
  DirectedTreeTopology::child_edges (TreeVertexId vertex) const {
    return m_child_edges.at (vertex);
  }

  bool DirectedTreeTopology::is_tip (TreeVertexId vertex) const {
    return child_edges (vertex).empty ();
  }

  bool DirectedTreeTopology::is_valid () const {
    if (m_vertices.empty () || m_parent_edges[0] != no_tree_edge ||
        m_edges.size () + 1 != m_vertices.size ())
      return false;
    for (TreeVertexId vertex = 1; vertex < m_vertices.size (); ++vertex) {
      const TreeEdgeId parent = m_parent_edges[vertex];
      if (parent == no_tree_edge || m_edges[parent].child != vertex ||
          m_edges[parent].parent >= vertex)
        return false;
    }
    return true;
  }

  TreeVertexDomain::TreeVertexDomain (
    std::shared_ptr<const DirectedTreeTopology> topology)
      : m_topology (std::move (topology)) {
    if (!m_topology)
      throw std::invalid_argument ("Tree vertex domain needs a topology");
  }

  std::size_t TreeVertexDomain::size () const noexcept {
    return m_topology->vertex_count ();
  }

  const DirectedTreeTopology& TreeVertexDomain::topology () const noexcept {
    return *m_topology;
  }

  TreeEdgeDomain::TreeEdgeDomain (
    std::shared_ptr<const DirectedTreeTopology> topology)
      : m_topology (std::move (topology)) {
    if (!m_topology)
      throw std::invalid_argument ("Tree edge domain needs a topology");
  }

  std::size_t TreeEdgeDomain::size () const noexcept {
    return m_topology->edge_count ();
  }

  const DirectedTreeTopology& TreeEdgeDomain::topology () const noexcept {
    return *m_topology;
  }

  namespace {
    struct Construction {
      std::vector<TreeVertex> vertices { { 0, 1 } };
      std::vector<TreeEdge> edges;
      std::vector<TreeEdgeForm> forms;
    };

    Real unit_hash (std::uint32_t bits) {
      bits += 0x9e3779b9U;
      bits ^= bits >> 16;
      bits *= 0x7feb352dU;
      bits ^= bits >> 15;
      return Real (bits & 0x00ffffffU) / Real (0x01000000U);
    }

    TreeVertexId append_axis (Construction& tree,
                              TreeVertexId parent,
                              std::size_t branch_order,
                              std::size_t segment_count,
                              Angle azimuth,
                              Angle elevation,
                              std::uint32_t seed,
                              TreeOrgan organ) {
      TreeVertexId current = parent;
      for (std::size_t segment = 0; segment < segment_count; ++segment) {
        const TreeVertexId child = tree.vertices.size ();
        const std::uint64_t lineage = tree.vertices[current].lineage * 131 +
                                      segment + 17 * branch_order + seed;
        tree.vertices.push_back (
          { tree.vertices[current].generation + 1, lineage });
        tree.edges.push_back (
          { current, child, branch_order, segment != 0, organ });
        const Real sway = unit_hash (seed + 43 * segment) - 0.5f;
        tree.forms.push_back ({ azimuth + sway * 0.14f * angular::radian,
                                elevation + sway * 0.08f * angular::radian,
                                unit_hash (seed + 101 * segment) });
        current = child;

        if (branch_order == 0 && segment >= 1 && segment + 1 < segment_count) {
          const std::size_t lateral_count = segment % 2 == 0 ? 2 : 1;
          for (std::size_t lateral = 0; lateral < lateral_count; ++lateral) {
            const Real turn = 2.399963f * Real (2 * segment + lateral);
            append_axis (tree,
                         current,
                         1,
                         4 + (segment % 2),
                         azimuth + turn * angular::radian,
                         (0.42f + 0.035f * Real (segment)) * angular::radian,
                         seed + 1009 * segment + 313 * lateral,
                         organ);
          }
        } else if (branch_order == 1 && (segment == 1 || segment == 3) &&
                   segment + 1 < segment_count) {
          const Real side = segment == 1 ? -1.0f : 1.0f;
          append_axis (tree,
                       current,
                       2,
                       2,
                       azimuth + side * 0.78f * angular::radian,
                       elevation +
                         (organ == TreeOrgan::shoot ? 0.18f : -0.18f) *
                           angular::radian,
                       seed + 2053 * segment,
                       organ);
        }
      }
      return current;
    }

    Construction make_tree (std::uint32_t seed) {
      Construction tree;
      const std::size_t trunk_segments = 10 + seed % 3;
      append_axis (tree,
                   0,
                   0,
                   trunk_segments,
                   0.0f * angular::radian,
                   1.48f * angular::radian,
                   seed,
                   TreeOrgan::shoot);
      const std::size_t root_count = 5 + (seed >> 4) % 3;
      for (std::size_t root = 0; root < root_count; ++root) {
        const Real turn = 2.399963f * Real (root);
        append_axis (tree,
                     0,
                     1,
                     4 + root % 2,
                     turn * angular::radian,
                     (-0.32f - 0.045f * Real (root % 3)) * angular::radian,
                     seed ^ (0x7001U + 977 * static_cast<std::uint32_t> (root)),
                     TreeOrgan::root);
      }
      return tree;
    }
  }

  Tree::Tree (std::uint32_t seed)
      : Tree ([seed] {
          Construction construction = make_tree (seed);
          auto topology = std::make_shared<const DirectedTreeTopology> (
            std::move (construction.vertices), std::move (construction.edges));
          return std::tuple (std::move (topology),
                             std::move (construction.forms));
        }()) {}

  Tree::Tree (std::tuple<std::shared_ptr<const DirectedTreeTopology>,
                         std::vector<TreeEdgeForm>> construction)
      : m_topology (std::move (std::get<0> (construction))),
        m_vertices (TreeVertexDomain (m_topology)),
        m_edges (TreeEdgeDomain (m_topology)),
        m_forms (std::move (std::get<1> (construction))) {
    auto& water = get<water_potential> (m_vertices);
    auto& sugar = get<sugar_potential> (m_vertices);
    auto& vigor = get<bud_vigor> (m_vertices);

    std::vector<Real> terminal_supply (m_topology->vertex_count (), 0.0f);
    std::size_t maximum_generation = 1;
    for (TreeVertexId vertex = 0; vertex < m_topology->vertex_count ();
         ++vertex) {
      maximum_generation =
        std::max (maximum_generation, m_topology->vertex (vertex).generation);
      if (m_topology->is_tip (vertex))
        terminal_supply[vertex] =
          0.75f + 0.5f * unit_hash (static_cast<std::uint32_t> (
                           m_topology->vertex (vertex).lineage));
    }
    const std::vector<Real> supported_tips = accumulate_along_tree (
      *m_topology,
      terminal_supply,
      TreeFlowDirection::toward_root,
      [] (Real first, Real second) { return first + second; });
    const Real total_supply = std::max (1.0f, supported_tips.front ());

    for (TreeVertexId vertex = 0; vertex < m_topology->vertex_count ();
         ++vertex) {
      const Real depth = Real (m_topology->vertex (vertex).generation) /
                         Real (maximum_generation);
      water[vertex] = (1.0f - 0.72f * depth) * mp_units::one;
      sugar[vertex] =
        std::min (1.0f, supported_tips[vertex] / total_supply) * mp_units::one;
      const TreeEdgeId parent = m_topology->parent_edge (vertex);
      const bool leaf_bearing =
        parent != no_tree_edge &&
        m_topology->edge (parent).organ == TreeOrgan::shoot &&
        (m_topology->is_tip (vertex) ||
         m_topology->edge (parent).branch_order >= 2);
      vigor[vertex] =
        leaf_bearing ? std::max (0.55f, terminal_supply[vertex]) * mp_units::one
                     : 0.0f * mp_units::one;
    }

    auto& length = get<branch_rest_length> (m_edges);
    auto& radius = get<branch_radius> (m_edges);
    auto& flexibility = get<branch_flexibility> (m_edges);
    auto& xylem = get<xylem_flux> (m_edges);
    auto& phloem = get<phloem_flux> (m_edges);
    for (TreeEdgeId id = 0; id < m_topology->edge_count (); ++id) {
      const TreeEdge& edge = m_topology->edge (id);
      const Real load = std::max (0.2f, supported_tips[edge.child]);
      const Real order = Real (edge.branch_order);
      length[id] = (0.72f - 0.17f * order) *
                   (0.94f + 0.12f * m_forms[id].material_seed) * m;
      radius[id] = 0.032f * std::pow (load, 0.46f) * m;
      flexibility[id] =
        std::clamp (0.18f + 0.23f * order + 0.42f / (1.0f + load), 0.0f, 1.0f) *
        mp_units::one;
      const Real direction = edge.organ == TreeOrgan::shoot ? 1.0f : -1.0f;
      xylem[id] = direction * load * 0.055f * mp_units::one / s;
      phloem[id] = -direction * load * 0.042f * mp_units::one / s;
    }
  }

  const DirectedTreeTopology& Tree::topology () const noexcept {
    return *m_topology;
  }

  const Tree::VertexState& Tree::vertices () const noexcept {
    return m_vertices;
  }

  const Tree::EdgeState& Tree::edges () const noexcept {
    return m_edges;
  }

  const TreeEdgeForm& Tree::form (TreeEdgeId edge) const {
    return m_forms.at (edge);
  }

  std::size_t Tree::tip_count () const {
    return std::ranges::count_if (
      std::views::iota (TreeVertexId (0), topology ().vertex_count ()),
      [this] (TreeVertexId vertex) { return topology ().is_tip (vertex); });
  }
}
