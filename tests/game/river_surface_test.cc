#include <moppe/game/river_surface.hh>
#include <moppe/terrain/river.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

using namespace moppe;

namespace {
  terrain::RiverAlignmentPoint river_test_point (float x,
                                                 float z,
                                                 float flow_distance,
                                                 float area,
                                                 float standing_water = 0.0f) {
    return { .x_m = x,
             .z_m = z,
             .flow_distance_m = flow_distance,
             .contributing_area_m2 = area,
             .slope = 0.05f,
             .standing_water = standing_water,
             .water_level_m = 6.0f };
  }

  terrain::RiverReach reach_with_alignment () {
    terrain::RiverReach reach;
    reach.id = 0;
    reach.upstream_body = terrain::no_water_body;
    reach.downstream_body = terrain::no_water_body;
    reach.downstream_reach = terrain::RiverReach::no_id;
    reach.alignment.points = { river_test_point (40, 10, -30, 25000),
                               river_test_point (40, 20, -20, 100000),
                               river_test_point (40, 30, -10, 400000, 0.35f),
                               river_test_point (40, 40, 0, 1000000, 1.0f) };
    for (std::size_t i = 0; i < reach.alignment.points.size (); ++i)
      reach.alignment.points[i].distance_m = static_cast<float> (10 * i);
    reach.alignment.length = 30.0 * mp_units::si::metre;
    return reach;
  }
}

MOPPE_TEST (visible_river_area_scales_with_the_terrain_cells) {
  const terrain::TerrainGrid grid { .width = 9,
                                    .height = 9,
                                    .spacing_x = 2.0f * mp_units::si::metre,
                                    .spacing_y = 3.0f * mp_units::si::metre };

  MOPPE_CHECK_NEAR (
    square_meters_value (terrain::visible_river_minimum_area (grid)),
    166666.67f,
    1.0f);
}

MOPPE_TEST (river_ribbons_are_dense_widen_downstream_and_fade_at_mouths) {
  map::RandomHeightMap map (9, 9, Vec3 (80, 20, 80), 0);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.3f);
  map.recompute_normals ();
  terrain::RiverNetwork rivers;
  terrain::RiverReach reach = reach_with_alignment ();
  // A transient or ribbon-owned upstream body does not supply a visible
  // standing-water sheet. Alignment state, rather than the body id alone,
  // decides whether this root still needs an emergence taper.
  reach.upstream_body = 17;
  rivers.reaches.push_back (std::move (reach));

  const render::DrawList draw = game::build_river_ribbons (map, rivers);

  MOPPE_CHECK (draw.runs ().size () == 1);
  MOPPE_CHECK (draw.vertices ().size () == 108);
  float upstream_min_x = std::numeric_limits<float>::infinity ();
  float upstream_max_x = -std::numeric_limits<float>::infinity ();
  float downstream_min_x = std::numeric_limits<float>::infinity ();
  float downstream_max_x = -std::numeric_limits<float>::infinity ();
  bool mouth_is_transparent = true;
  bool has_readable_center = false;
  for (const render::Vertex& vertex : draw.vertices ()) {
    MOPPE_CHECK (std::isfinite (vertex.px));
    MOPPE_CHECK (std::isfinite (vertex.py));
    MOPPE_CHECK (std::isfinite (vertex.pz));
    if (std::abs (vertex.v + 30.0f) < 1e-5f) {
      upstream_min_x = std::min (upstream_min_x, vertex.px);
      upstream_max_x = std::max (upstream_max_x, vertex.px);
    }
    if (std::abs (vertex.v + 10.0f) < 1e-5f) {
      downstream_min_x = std::min (downstream_min_x, vertex.px);
      downstream_max_x = std::max (downstream_max_x, vertex.px);
    }
    if (std::abs (vertex.v) < 1e-5f && vertex.color.a != 0)
      mouth_is_transparent = false;
    if (vertex.color.a >= 128)
      has_readable_center = true;
  }
  MOPPE_CHECK (downstream_max_x - downstream_min_x >
               upstream_max_x - upstream_min_x);
  MOPPE_CHECK (mouth_is_transparent);
  MOPPE_CHECK (has_readable_center);
}

MOPPE_TEST (headwater_ribbons_emerge_from_a_point) {
  map::RandomHeightMap map (9, 9, Vec3 (80, 20, 80), 0);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.3f);
  map.recompute_normals ();
  terrain::RiverNetwork rivers;
  rivers.reaches.push_back (reach_with_alignment ());

  const render::DrawList draw = game::build_river_ribbons (map, rivers);

  float source_min_x = std::numeric_limits<float>::infinity ();
  float source_max_x = -std::numeric_limits<float>::infinity ();
  float emerging_min_x = std::numeric_limits<float>::infinity ();
  float emerging_max_x = -std::numeric_limits<float>::infinity ();
  for (const render::Vertex& vertex : draw.vertices ()) {
    if (std::abs (vertex.v + 30.0f) < 1e-5f) {
      source_min_x = std::min (source_min_x, vertex.px);
      source_max_x = std::max (source_max_x, vertex.px);
    }
    if (std::abs (vertex.v + 20.0f) < 1e-5f) {
      emerging_min_x = std::min (emerging_min_x, vertex.px);
      emerging_max_x = std::max (emerging_max_x, vertex.px);
    }
  }
  MOPPE_CHECK_NEAR (source_max_x - source_min_x, 0.0f, 1e-5f);
  MOPPE_CHECK (emerging_max_x - emerging_min_x > 0.5f);
}

MOPPE_TEST (river_flow_coordinates_join_continuously_at_confluences) {
  map::RandomHeightMap map (9, 9, Vec3 (80, 20, 80), 0);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.3f);
  map.recompute_normals ();

  terrain::RiverReach tributary = reach_with_alignment ();
  tributary.id = 0;
  tributary.downstream_reach = 1;
  tributary.alignment.points = { river_test_point (20, 10, -40, 25000),
                                 river_test_point (40, 20, -20, 100000) };
  terrain::RiverReach trunk = reach_with_alignment ();
  trunk.id = 1;
  trunk.alignment.points = { river_test_point (40, 20, -20, 200000),
                             river_test_point (40, 30, -10, 400000),
                             river_test_point (40, 40, 0, 1000000, 1.0f) };
  terrain::RiverNetwork rivers;
  rivers.reaches = { tributary, trunk };

  const render::DrawList draw = game::build_river_ribbons (map, rivers);

  int junction_vertices = 0;
  for (const render::Vertex& vertex : draw.vertices ())
    if (std::abs (vertex.v + 20.0f) < 1e-5f)
      ++junction_vertices;
  MOPPE_CHECK (junction_vertices > 6);
}

MOPPE_TEST (periodic_river_junctions_use_the_nearest_image) {
  map::RandomHeightMap map (
    9, 9, Vec3 (80, 20, 80), 0, terrain::Topology::Torus);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.3f);
  map.recompute_normals ();

  terrain::RiverReach tributary = reach_with_alignment ();
  tributary.id = 0;
  tributary.downstream_reach = 1;
  tributary.alignment.points = { river_test_point (68, 20, -30, 25000),
                                 river_test_point (78, 20, -20, 100000) };
  terrain::RiverReach trunk = reach_with_alignment ();
  trunk.id = 1;
  trunk.alignment.points = { river_test_point (-2, 20, -20, 200000),
                             river_test_point (8, 30, -10, 400000),
                             river_test_point (18, 40, 0, 1000000, 1.0f) };
  terrain::RiverNetwork rivers;
  rivers.reaches = { tributary, trunk };

  const render::DrawList draw = game::build_river_ribbons (map, rivers);
  const auto& vertices = draw.vertices ();
  for (std::size_t triangle = 0; triangle < vertices.size (); triangle += 3)
    for (int edge = 0; edge < 3; ++edge) {
      const render::Vertex& a = vertices[triangle + edge];
      const render::Vertex& b = vertices[triangle + (edge + 1) % 3];
      MOPPE_CHECK (std::hypot (b.px - a.px, b.pz - a.pz) < 25.0f);
    }
}

MOPPE_TEST (confluences_share_one_downstream_cross_section) {
  map::RandomHeightMap map (9, 9, Vec3 (80, 20, 80), 0);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.3f);
  map.recompute_normals ();

  terrain::RiverReach left = reach_with_alignment ();
  left.id = 0;
  left.downstream_reach = 2;
  left.alignment.points = { river_test_point (20, 10, -40, 25000),
                            river_test_point (40, 20, -20, 100000) };
  terrain::RiverReach right = reach_with_alignment ();
  right.id = 1;
  right.downstream_reach = 2;
  right.alignment.points = { river_test_point (60, 10, -40, 25000),
                             river_test_point (40, 20, -20, 100000) };
  terrain::RiverReach trunk = reach_with_alignment ();
  trunk.id = 2;
  trunk.alignment.points = { river_test_point (40, 20, -20, 200000),
                             river_test_point (40, 30, -10, 400000),
                             river_test_point (40, 40, 0, 1000000, 1.0f) };
  terrain::RiverNetwork rivers;
  rivers.reaches = { left, right, trunk };

  const render::DrawList draw = game::build_river_ribbons (map, rivers);

  std::vector<std::pair<int, int>> junction_positions;
  for (const render::Vertex& vertex : draw.vertices ())
    if (std::abs (vertex.v + 20.0f) < 1e-5f)
      junction_positions.emplace_back (
        static_cast<int> (std::lround (1000.0f * vertex.px)),
        static_cast<int> (std::lround (1000.0f * vertex.pz)));
  std::sort (junction_positions.begin (), junction_positions.end ());
  junction_positions.erase (
    std::unique (junction_positions.begin (), junction_positions.end ()),
    junction_positions.end ());
  MOPPE_CHECK (junction_positions.size () == 7);
}

MOPPE_TEST (river_ribbons_encode_rapids_depth_and_waterfalls) {
  map::RandomHeightMap map (9, 9, Vec3 (80, 20, 80), 0);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.3f);
  map.recompute_normals ();
  terrain::RiverReach reach = reach_with_alignment ();
  reach.alignment.points[1].waterfall = 1.0f;
  terrain::RiverNetwork rivers;
  rivers.reaches.push_back (std::move (reach));

  const render::DrawList draw = game::build_river_ribbons (map, rivers);

  bool rapid = false;
  bool depth = false;
  bool waterfall = false;
  for (const render::Vertex& vertex : draw.vertices ()) {
    rapid = rapid || vertex.color.r > 0;
    depth = depth || vertex.color.g > 0;
    waterfall = waterfall || vertex.color.b == 255;
  }
  MOPPE_CHECK (rapid);
  MOPPE_CHECK (depth);
  MOPPE_CHECK (waterfall);
}
