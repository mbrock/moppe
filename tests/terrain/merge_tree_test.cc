#include <moppe/terrain/flood.hh>
#include <moppe/terrain/merge_tree.hh>

#include <moppe/map/surface.hh>
#include <moppe/map/terrain_generation.hh>

#include <tests/test.hh>

#include <array>
#include <bit>
#include <cstdint>
#include <vector>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  // The oracle: the merge-tree flood must reproduce the priority
  // flood's minimax surface bit-for-bit, its ocean component, and its
  // deterministic fallbacks.
  void check_matches_priority_flood (const TerrainElevations auto& terrain,
                                     float sea_level) {
    const FloodField oracle = analyze_standing_water (terrain, sea_level);
    const MergeTree tree = build_merge_tree (terrain);
    const MergeTreeFlood flood =
      flood_from_merge_tree (tree, terrain, sea_level);

    MOPPE_CHECK (flood.has_ocean == oracle.has_ocean);
    MOPPE_CHECK (oracle.outlets.size () == 1);
    MOPPE_CHECK (flood.root_cell == oracle.outlets.front ());
    const std::span<const float> expected = oracle.water_level.values ();
    const std::span<const float> actual = flood.water_level.values ();
    MOPPE_CHECK (expected.size () == actual.size ());
    for (std::size_t i = 0; i < expected.size (); ++i) {
      MOPPE_CHECK (std::bit_cast<std::uint32_t> (expected[i]) ==
                   std::bit_cast<std::uint32_t> (actual[i]));
      MOPPE_CHECK (flood.ocean[i] == oracle.ocean[i]);
    }
  }
}

MOPPE_TEST (merge_tree_flood_matches_a_basin_and_its_spill) {
  const std::array heights { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 3.f, 2.f, 3.f,
                             0.f, 0.f, 3.f, 1.f, 3.f, 0.f, 0.f, 3.f, 3.f,
                             3.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (5, 5), heights);
  check_matches_priority_flood (terrain, 0.0f);

  const MergeTree tree = build_merge_tree (terrain);
  const MergeTreeFlood flood = flood_from_merge_tree (tree, terrain, 0.0f);
  // The enclosed pit at (2,2) fills to its spill saddle.
  MOPPE_CHECK_NEAR (flood.water_level.at (2, 2), 2.0f, 0.0f);
  // Two leaves (outer plain, inner pit) joined by one saddle.
  MOPPE_CHECK (tree.nodes.size () == 3);
}

MOPPE_TEST (merge_tree_flood_matches_an_all_land_torus) {
  const std::array heights { 5.f, 4.f, 7.f, 3.f };
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (2, 2), heights);
  check_matches_priority_flood (terrain, -1.0f);

  const MergeTree tree = build_merge_tree (terrain);
  const MergeTreeFlood flood = flood_from_merge_tree (tree, terrain, -1.0f);
  MOPPE_CHECK (!flood.has_ocean);
  // The endorheic fallback roots at the deterministic global minimum:
  // height 3 at cell 3.
  MOPPE_CHECK (flood.root_cell == 3u);
}

MOPPE_TEST (merge_tree_flood_matches_a_nearly_all_ocean_world) {
  const std::array heights {
    -3.f, -3.f, -3.f, -3.f, 5.f, -3.f, -3.f, -3.f, -3.f
  };
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (3, 3), heights);
  check_matches_priority_flood (terrain, 0.0f);
}

MOPPE_TEST (merge_tree_flood_matches_generated_worlds_at_many_levels) {
  for (const int seed : { 7, 123, 4041 }) {
    map::Surface map (65, 65, Vec3 (100, 20, 100));
    map::initialize_terrain (
      map, Seed { static_cast<std::uint32_t> (seed) }, 50.0f * u::m);
    const auto& terrain = map.geometry ();
    for (const float sea_level : { 0.05f, 50.0f / 650.0f, 0.3f, 0.95f })
      check_matches_priority_flood (terrain, sea_level);
  }
}

MOPPE_TEST (merge_tree_construction_is_deterministic) {
  map::Surface map (65, 65, Vec3 (100, 20, 100));
  map::initialize_terrain (map, Seed { 9 }, 50.0f * u::m);
  const auto& terrain = map.geometry ();
  const MergeTree first = build_merge_tree (terrain);
  const MergeTree second = build_merge_tree (terrain);
  MOPPE_CHECK (first.nodes.size () == second.nodes.size ());
  for (std::size_t i = 0; i < first.nodes.size (); ++i) {
    MOPPE_CHECK (first.nodes[i].birth == second.nodes[i].birth);
    MOPPE_CHECK (first.nodes[i].origin == second.nodes[i].origin);
    MOPPE_CHECK (first.nodes[i].parent == second.nodes[i].parent);
  }
  MOPPE_CHECK (first.cell_node == second.cell_node);
}
