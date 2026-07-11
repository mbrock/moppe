#include <moppe/game/river_surface.hh>
#include <moppe/game/water_capture.hh>

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
    .spill_receiver = { 1, 5, 2, 3, 4, 9, 6, 7,
			8, 9, 10, 11, 12, 13, 14, 15 },
    .outlets = { 2, 3, 4, 6, 9 }
  };
  const terrain::LakeCensus census {
    .body = std::vector<std::uint32_t>
      (count, terrain::LakeCensus::dry)
  };
  std::vector<float> area (count, 25000.0f);
  area[1] = 100000.0f;
  area[5] = 400000.0f;
  area[9] = 1000000.0f;
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
      .cells = { 0, 1, 5 },
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
  MOPPE_CHECK (draw.vertices ().size () == 36);
  const render::Vertex& first_left = draw.vertices ()[0];
  const render::Vertex& first_right = draw.vertices ()[1];
  const render::Vertex& second_right = draw.vertices ()[2];
  const render::Vertex& second_left = draw.vertices ()[5];
  const float first_width = std::hypot
    (first_left.px - first_right.px, first_left.pz - first_right.pz);
  const float second_width = std::hypot
    (second_left.px - second_right.px, second_left.pz - second_right.pz);
  MOPPE_CHECK (second_width > first_width);
  MOPPE_CHECK_NEAR
    (0.5f * (first_left.px + first_right.px), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR
    (0.5f * (first_left.pz + first_right.pz), 0.0f, 1e-6f);
  const render::Vertex& final_right = draw.vertices ()[32];
  const render::Vertex& final_left = draw.vertices ()[35];
  MOPPE_CHECK_NEAR
    (0.5f * (final_left.px + final_right.px), 10.0f, 1e-5f);
  MOPPE_CHECK_NEAR
    (0.5f * (final_left.pz + final_right.pz), 20.0f, 1e-5f);
  const render::Vertex& curved_right = draw.vertices ()[14];
  const render::Vertex& curved_left = draw.vertices ()[17];
  const float curved_x = 0.5f * (curved_right.px + curved_left.px);
  MOPPE_CHECK (curved_x > 10.1f);
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
    .foot_cell = 5,
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
  MOPPE_CHECK (fall_draw.vertices ().size () == 36);
  MOPPE_CHECK (waterfall_vertices > 0);
}

MOPPE_TEST (river_ribbons_join_and_dissipate_along_standing_water_flow) {
  constexpr std::size_t count = 25;
  map::RandomHeightMap map (5, 5, Vector3D (50, 10, 50), 0);
  for (int z = 0; z < map.height (); ++z)
    for (int x = 0; x < map.width (); ++x)
      map.set (x, z, 0.3f);
  map.recompute_normals ();

  const terrain::TerrainGrid grid = map.terrain_view ().grid ();
  const terrain::Domain2D domain { .width = 5, .height = 5 };
  std::vector<std::uint32_t> receiver (count);
  for (std::uint32_t cell = 0; cell < count; ++cell)
    receiver[cell] = cell;
  receiver[1] = 6;
  receiver[6] = 7;
  receiver[2] = 7;
  receiver[7] = 8;
  receiver[8] = 9;
  receiver[9] = 14;
  receiver[14] = 19;
  receiver[19] = 24;
  std::vector<float> water_level (count, 0.0f);
  std::vector<float> water_depth (count, 0.0f);
  std::vector<std::uint32_t> body
    (count, terrain::LakeCensus::dry);
  for (const std::uint32_t cell : { 14u, 19u, 24u }) {
    water_level[cell] = 0.3f;
    water_depth[cell] = 0.1f;
    body[cell] = 0;
  }
  const terrain::FloodField flood {
    .source_grid = grid,
    .sea_level = 0.0f,
    .has_ocean = false,
    .water_level = terrain::ScalarRaster
      (domain, std::move (water_level)),
    .water_depth = terrain::ScalarRaster
      (domain, std::move (water_depth)),
    .ocean = std::vector<std::uint8_t> (count, 0),
    .spill_receiver = receiver,
    .outlets = { 24 }
  };
  const terrain::LakeCensus census { .body = std::move (body) };
  std::vector<float> area (count, 25000.0f);
  area[7] = 100000.0f;
  area[8] = 250000.0f;
  area[9] = 500000.0f;
  area[14] = 600000.0f;
  area[19] = 700000.0f;
  area[24] = 800000.0f;
  const terrain::DrainageGraph drainage {
    .source_grid = grid,
    .receiver = receiver,
    .slope = terrain::ScalarRaster
      (domain, std::vector<float> (count, 0.02f)),
    .contributing_area = terrain::ScalarRaster (domain, std::move (area)),
    .basin = std::vector<std::uint32_t> (count, 24),
    .sinks = { 24 }
  };
  const terrain::RiverNetwork rivers {
    .minimum_area_m2 = 25000.0f,
    .reach_by_cell = std::vector<std::uint32_t>
      (count, terrain::RiverReach::no_id),
    .waterfall_by_cell = std::vector<std::uint32_t>
      (count, terrain::Waterfall::no_id),
    .reaches = {
      {
	.id = 0,
	.cells = { 1, 6 },
	.upstream_body = terrain::RiverReach::no_id,
	.downstream_body = terrain::RiverReach::no_id,
	.downstream_ocean = false,
	.downstream_reach = 2
      },
      {
	.id = 1,
	.cells = { 2 },
	.upstream_body = terrain::RiverReach::no_id,
	.downstream_body = terrain::RiverReach::no_id,
	.downstream_ocean = false,
	.downstream_reach = 2
      },
      {
	.id = 2,
	.cells = { 7, 8, 9 },
	.upstream_body = terrain::RiverReach::no_id,
	.downstream_body = 0,
	.downstream_ocean = false,
	.downstream_reach = terrain::RiverReach::no_id
      }
    }
  };

  const render::DrawList draw = game::build_river_ribbons
    (map, flood, census, drainage, rivers);

  MOPPE_CHECK (draw.runs ().size () == 1);
  MOPPE_CHECK (draw.vertices ().size () == 84);
  float maximum_z = 0.0f;
  std::size_t soft_vertices = 0;
  std::size_t junction_fade_vertices = 0;
  for (const render::Vertex& vertex : draw.vertices ()) {
    maximum_z = std::max (maximum_z, vertex.pz);
    if (vertex.color.a == 0)
      ++soft_vertices;
    if (vertex.color.a == render::Color::quantize (0.12f))
      ++junction_fade_vertices;
  }
  MOPPE_CHECK (maximum_z >= 30.0f);
  MOPPE_CHECK (soft_vertices > 0);
  MOPPE_CHECK (junction_fade_vertices > 0);

  terrain::DrainageGraph shortcut = drainage;
  shortcut.receiver[14] = 24;
  const render::DrawList shortcut_draw = game::build_river_ribbons
    (map, flood, census, shortcut, rivers);
  MOPPE_CHECK (shortcut_draw.vertices ().size () == 72);
  for (const render::Vertex& vertex : shortcut_draw.vertices ())
    MOPPE_CHECK (vertex.pz < 25.0f);

  const std::optional<game::WaterInspection> confluence =
    game::choose_water_inspection
      (game::WaterShot::Confluence, map, flood, census, drainage, rivers);
  const std::optional<game::WaterInspection> mouth =
    game::choose_water_inspection
      (game::WaterShot::Mouth, map, flood, census, drainage, rivers);
  MOPPE_CHECK (confluence.has_value ());
  MOPPE_CHECK (confluence->cell == 7);
  MOPPE_CHECK (mouth.has_value ());
  MOPPE_CHECK (mouth->cell == 14);
  MOPPE_CHECK (std::isfinite (mouth->eye.x));
  MOPPE_CHECK (std::isfinite (mouth->eye.y));
  MOPPE_CHECK (std::isfinite (mouth->eye.z));
  MOPPE_CHECK
    (game::parse_water_shot ("fall") == game::WaterShot::Waterfall);
  MOPPE_CHECK (!game::parse_water_shot ("bogus").has_value ());
}
