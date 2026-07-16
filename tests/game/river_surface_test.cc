#include <moppe/game/river_surface.hh>
#include <moppe/terrain/river.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace moppe;

namespace {
  terrain::RiverAlignmentPoint river_test_point (
    float x,
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
  rivers.reaches.push_back (reach_with_alignment ());

  const render::DrawList draw = game::build_river_ribbons (map, rivers);

  MOPPE_CHECK (draw.runs ().size () == 1);
  MOPPE_CHECK (draw.vertices ().size () == 108);
  float upstream_min_x = std::numeric_limits<float>::infinity ();
  float upstream_max_x = -std::numeric_limits<float>::infinity ();
  float downstream_min_x = std::numeric_limits<float>::infinity ();
  float downstream_max_x = -std::numeric_limits<float>::infinity ();
  bool mouth_is_transparent = true;
  bool has_opaque_center = false;
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
    if (vertex.color.a == 255)
      has_opaque_center = true;
  }
  MOPPE_CHECK (downstream_max_x - downstream_min_x >
               upstream_max_x - upstream_min_x);
  MOPPE_CHECK (mouth_is_transparent);
  MOPPE_CHECK (has_opaque_center);
}

MOPPE_TEST (river_flow_coordinates_join_continuously_at_confluences) {
  map::RandomHeightMap map (9, 9, Vec3 (80, 20, 80), 0);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.3f);
  map.recompute_normals ();

  terrain::RiverReach tributary = reach_with_alignment ();
  tributary.id = 0;
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
