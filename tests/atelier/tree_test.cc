#include <atelier/tree.hh>
#include <atelier/tree_embedding.hh>

#include <tests/test.hh>

#include <vector>

using namespace atelier;
using namespace mp_units::si::unit_symbols;

MOPPE_TEST (tree_is_one_valid_oriented_complex) {
  const Tree tree;
  const DirectedTreeTopology& topology = tree.topology ();

  MOPPE_CHECK (topology.is_valid ());
  MOPPE_CHECK (topology.edge_count () + 1 == topology.vertex_count ());
  MOPPE_CHECK (tree.tip_count () > 20);
  MOPPE_CHECK (tree.vertices ().size () == topology.vertex_count ());
  MOPPE_CHECK (tree.edges ().size () == topology.edge_count ());

  std::size_t shoot_edges = 0;
  std::size_t root_edges = 0;
  for (TreeEdgeId edge = 0; edge < topology.edge_count (); ++edge)
    if (topology.edge (edge).organ == TreeOrgan::shoot)
      ++shoot_edges;
    else
      ++root_edges;
  MOPPE_CHECK (shoot_edges > 0);
  MOPPE_CHECK (root_edges > 0);
}

MOPPE_TEST (one_accumulation_law_runs_with_or_against_the_arrows) {
  const DirectedTreeTopology topology (
    { { 0, 1 }, { 1, 2 }, { 1, 3 }, { 2, 4 } },
    { { 0, 1, 0, true }, { 0, 2, 1, false }, { 1, 3, 0, true } });

  const auto gathered = accumulate_along_tree (
    topology,
    std::vector<int> { 0, 0, 2, 3 },
    TreeFlowDirection::toward_root,
    [] (int first, int second) { return first + second; });
  MOPPE_CHECK (gathered[0] == 5);
  MOPPE_CHECK (gathered[1] == 3);

  const auto distributed = accumulate_along_tree (
    topology,
    std::vector<int> { 7, 1, 2, 3 },
    TreeFlowDirection::away_from_root,
    [] (int local, int inherited) { return local + inherited; });
  MOPPE_CHECK (distributed[1] == 8);
  MOPPE_CHECK (distributed[2] == 9);
  MOPPE_CHECK (distributed[3] == 11);
}

MOPPE_TEST (xylem_and_phloem_share_edges_with_opposite_signs) {
  const Tree tree;
  const auto& xylem = get<xylem_flux> (tree.edges ());
  const auto& phloem = get<phloem_flux> (tree.edges ());
  for (TreeEdgeId edge = 0; edge < tree.topology ().edge_count (); ++edge) {
    const Real direction =
      tree.topology ().edge (edge).organ == TreeOrgan::shoot ? 1.0f : -1.0f;
    MOPPE_CHECK (direction * xylem[edge] > 0.0f * mp_units::one / s);
    MOPPE_CHECK (direction * phloem[edge] < 0.0f * mp_units::one / s);
  }
}

MOPPE_TEST (diagram_and_wind_embeddings_preserve_the_organism) {
  const Tree tree;
  const EmbeddedTree diagram =
    embed_tree (tree, TreeEmbeddingKind::diagram, 3.0f * s, 1.5f);
  const EmbeddedTree wind =
    embed_tree (tree, TreeEmbeddingKind::wind_bent, 3.0f * s, 1.5f);

  MOPPE_CHECK (diagram.branches.size () == tree.topology ().edge_count ());
  MOPPE_CHECK (wind.branches.size () == tree.topology ().edge_count ());
  MOPPE_CHECK (diagram.buds.size () == tree.tip_count ());
  MOPPE_CHECK (wind.buds.size () > 3 * tree.tip_count ());

  std::size_t root_endpoints = 0;
  for (const EmbeddedTreeBud& bud : diagram.buds)
    root_endpoints += bud.organ == TreeOrgan::root ? 1 : 0;
  MOPPE_CHECK (root_endpoints > 0);
  MOPPE_CHECK (root_endpoints < diagram.buds.size ());
}
