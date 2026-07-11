#include <moppe/game/river_surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace moppe;

MOPPE_TEST (visible_river_area_scales_with_the_terrain_cells) {
  const terrain::TerrainGrid grid {
    .width = 9,
    .height = 9,
    .spacing_x = 2.0f,
    .spacing_y = 3.0f
  };

  MOPPE_CHECK_NEAR
    (game::visible_river_minimum_area (grid), 6144.0f, 0.0f);
}

MOPPE_TEST (river_ribbons_follow_reaches_and_widen_with_catchment) {
  constexpr std::size_t count = 16;
  map::RandomHeightMap map (4, 4, Vector3D (40, 10, 40), 0);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.4f - 0.1f * x);
  map.recompute_normals ();

  const terrain::TerrainGrid grid = map.terrain_view ().grid ();
  const terrain::Domain2D domain { .width = 4, .height = 4 };
  const terrain::FloodField flood {
    .source_grid = grid,
    .sea_level = 0.0f,
    .has_ocean = false,
    .water_level = terrain::ScalarRaster
      (domain, std::vector<float> (count, 0.0f)),
    .water_depth = terrain::ScalarRaster
      (domain, std::vector<float> (count, 0.0f)),
    .ocean = std::vector<std::uint8_t> (count, 0),
    .spill_receiver = { 1, 2, 3, 3, 4, 5, 6, 7,
			8, 9, 10, 11, 12, 13, 14, 15 },
    .outlets = { 3 }
  };
  const terrain::LakeCensus census {
    .body = std::vector<std::uint32_t>
      (count, terrain::LakeCensus::dry)
  };
  std::vector<float> area (count, 25000.0f);
  area[1] = 100000.0f;
  area[2] = 400000.0f;
  area[3] = 1000000.0f;
  const terrain::DrainageGraph drainage {
    .source_grid = grid,
    .receiver = flood.spill_receiver,
    .slope = terrain::ScalarRaster
      (domain, std::vector<float> (count, 0.05f)),
    .contributing_area = terrain::ScalarRaster (domain, std::move (area)),
    .basin = std::vector<std::uint32_t> (count, 0),
    .sinks = { 3 }
  };
  const terrain::RiverNetwork rivers {
    .minimum_area_m2 = 25000.0f,
    .reach_by_cell = std::vector<std::uint32_t> (count, 0),
    .waterfall_by_cell = std::vector<std::uint32_t>
      (count, terrain::Waterfall::no_id),
    .reaches = {{
      .id = 0,
      .cells = { 0, 1, 2 },
      .upstream_body = terrain::RiverReach::no_id,
      .downstream_body = terrain::RiverReach::no_id,
      .downstream_ocean = false,
      .downstream_reach = terrain::RiverReach::no_id,
      .upstream_area_m2 = 25000.0f,
      .downstream_area_m2 = 400000.0f,
      .maximum_slope = 0.05f
    }}
  };

  const render::DrawList draw = game::build_river_ribbons
    (map, flood, census, drainage, rivers);

  MOPPE_CHECK (draw.runs ().size () == 1);
  MOPPE_CHECK (draw.vertices ().size () == 18);
  const render::Vertex& first_left = draw.vertices ()[0];
  const render::Vertex& first_right = draw.vertices ()[1];
  const render::Vertex& second_right = draw.vertices ()[2];
  const render::Vertex& second_left = draw.vertices ()[5];
  const float first_width = std::hypot
    (first_left.px - first_right.px, first_left.pz - first_right.pz);
  const float second_width = std::hypot
    (second_left.px - second_right.px, second_left.pz - second_right.pz);
  MOPPE_CHECK (second_width > first_width);
  for (const render::Vertex& vertex : draw.vertices ()) {
    MOPPE_CHECK (std::isfinite (vertex.px));
    MOPPE_CHECK (std::isfinite (vertex.py));
    MOPPE_CHECK (std::isfinite (vertex.pz));
  }

  terrain::RiverNetwork falling = rivers;
  falling.waterfall_by_cell[1] = 0;
  falling.waterfalls.push_back ({
    .id = 0,
    .reach_id = 0,
    .lip_cell = 1,
    .foot_cell = 2,
    .drop_m = 1.0f,
    .horizontal_distance_m = 10.0f,
    .slope = 0.1f,
    .contributing_area_m2 = 100000.0f
  });
  const render::DrawList fall_draw = game::build_river_ribbons
    (map, flood, census, drainage, falling);
  std::size_t waterfall_vertices = 0;
  for (const render::Vertex& vertex : fall_draw.vertices ())
    if (vertex.color.b == 255)
      ++waterfall_vertices;
  MOPPE_CHECK (fall_draw.vertices ().size () == 18);
  MOPPE_CHECK (waterfall_vertices > 0);
}
