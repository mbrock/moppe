#include <moppe/terrain/flood.hh>

#include <tests/test.hh>

#include <array>
#include <vector>

using namespace moppe::terrain;

MOPPE_TEST (priority_flood_fills_a_basin_to_its_lowest_spill) {
  const std::array heights { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 3.f, 2.f, 3.f,
                             0.f, 0.f, 3.f, 1.f, 3.f, 0.f, 0.f, 3.f, 3.f,
                             3.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
  const TerrainView terrain ({ .width = 5, .height = 5 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);

  MOPPE_CHECK_NEAR (flood.water_level.at (2, 2), 2.0f, 0.0f);
  MOPPE_CHECK_NEAR (flood.water_depth.at (2, 2), 1.0f, 0.0f);
  MOPPE_CHECK_NEAR (flood.water_depth.at (2, 1), 0.0f, 0.0f);
}

MOPPE_TEST (standing_sea_uses_the_global_water_plane) {
  const std::array heights { -2.f, -1.f, 1.f, 2.f };
  const TerrainView terrain ({ .width = 2, .height = 2 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);

  MOPPE_CHECK_NEAR (flood.water_level.at (0, 0), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (flood.water_depth.at (0, 0), 2.0f, 0.0f);
  MOPPE_CHECK_NEAR (flood.water_depth.at (1, 0), 1.0f, 0.0f);
  MOPPE_CHECK (flood.has_ocean);
  MOPPE_CHECK (flood.outlets.size () == 1);
  MOPPE_CHECK (flood.outlets[0] == 0);
  MOPPE_CHECK (flood.spill_receiver[1] == 0);
}

MOPPE_TEST (an_enclosed_below_sea_basin_is_not_a_second_ocean) {
  const std::array heights { -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, 3.f,  3.f, 3.f,
                             -1.f, -1.f, 3.f,  -2.f, 3.f,  -1.f, -1.f, 3.f, 3.f,
                             3.f,  -1.f, -1.f, -1.f, -1.f, -1.f, -1.f };
  const TerrainView terrain ({ .width = 5, .height = 5 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);

  MOPPE_CHECK (flood.outlets.size () == 1);
  MOPPE_CHECK_NEAR (flood.water_level.at (2, 2), 3.0f, 0.0f);
  MOPPE_CHECK (census.bodies.size () == 2);
  MOPPE_CHECK (census.bodies[0].ocean_connected);
  MOPPE_CHECK (!census.bodies[1].ocean_connected);
  MOPPE_CHECK (census.bodies[1].outlet_cell == 12);
  MOPPE_CHECK (census.bodies[1].spill_cell != WaterBody::no_cell);
}

MOPPE_TEST (periodic_flood_can_spill_across_the_duplicated_seam) {
  const std::array heights { 1.f, 3.f, 0.f, 1.f, 1.f, 3.f, 0.f, 1.f,
                             1.f, 3.f, 0.f, 1.f, 1.f, 3.f, 0.f, 1.f };
  const TerrainView terrain (
    { .width = 4, .height = 4, .topology = Topology::Torus }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);

  MOPPE_CHECK (flood.width () == 3);
  MOPPE_CHECK (flood.spill_receiver[0] == 2);
}

MOPPE_TEST (all_land_torus_uses_its_global_minimum_as_an_outlet) {
  const std::array heights { 4.f, 3.f, 2.f, 4.f, 4.f, 1.f, 3.f, 4.f,
                             4.f, 4.f, 4.f, 4.f, 4.f, 3.f, 2.f, 4.f };
  const TerrainView terrain (
    { .width = 4, .height = 4, .topology = Topology::Torus }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);

  MOPPE_CHECK (flood.outlets.size () == 1);
  MOPPE_CHECK (!flood.has_ocean);
  MOPPE_CHECK (flood.outlets[0] == 4);
  MOPPE_CHECK (flood.spill_receiver[4] == 4);
}

MOPPE_TEST (every_spill_receiver_path_reaches_an_outlet) {
  const std::array heights { 0.f, 4.f, 3.f, 0.f, 2.f, 1.f, 5.f, 2.f,
                             3.f, 2.f, 4.f, 3.f, 0.f, 4.f, 3.f, 0.f };
  const TerrainView terrain (
    { .width = 4, .height = 4, .topology = Topology::Torus }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const std::size_t count = flood.width () * flood.height ();

  for (std::uint32_t origin = 0; origin < count; ++origin) {
    std::uint32_t cell = origin;
    std::size_t steps = 0;
    while (flood.spill_receiver[cell] != cell && steps < count) {
      const std::uint32_t next = flood.spill_receiver[cell];
      MOPPE_CHECK (flood.water_level.values ()[next] <=
                   flood.water_level.values ()[cell]);
      cell = next;
      ++steps;
    }
    MOPPE_CHECK (steps < count);
  }
}

MOPPE_TEST (lake_census_measures_physical_area_depth_and_volume) {
  const std::array heights { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 3.f, 2.f, 3.f,
                             0.f, 0.f, 3.f, 1.f, 3.f, 0.f, 0.f, 3.f, 3.f,
                             3.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
  const TerrainView terrain ({ .width = 5,
                               .height = 5,
                               .spacing_x = 2.0f * mp_units::si::metre,
                               .height_scale = 10.0f * mp_units::si::metre },
                             heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);

  MOPPE_CHECK (census.bodies.size () == 1);
  MOPPE_CHECK (census.bodies[0].cells == cell_count (1));
  MOPPE_CHECK_NEAR (
    moppe::square_meters_value (census.bodies[0].area), 2.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    moppe::meters_value (census.bodies[0].maximum_depth), 10.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    moppe::meters_value (census.bodies[0].mean_depth), 10.0f, 0.0f);
  MOPPE_CHECK_NEAR (
    moppe::cubic_meters_value (census.bodies[0].volume), 20.0f, 0.0f);
  MOPPE_CHECK (!census.bodies[0].ocean_connected);
  MOPPE_CHECK (census.bodies[0].outlet_cell == 12);
  MOPPE_CHECK (census.bodies[0].spill_cell == 7);
  MOPPE_CHECK (census.bodies[0].classification == WaterBodyClass::Puddle);
}

MOPPE_TEST (lake_census_uses_the_final_exit_from_a_reentered_body) {
  const TerrainGrid grid { .width = 3, .height = 3 };
  const std::vector<float> level (9, 1.0f);
  const std::vector<float> depth { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f };
  const FloodField flood { .source_grid = grid,
                           .sea_level = 0.0f,
                           .has_ocean = false,
                           .water_level =
                             ScalarRaster ({ .width = 3, .height = 3 }, level),
                           .water_depth =
                             ScalarRaster ({ .width = 3, .height = 3 }, depth),
                           .ocean = std::vector<std::uint8_t> (9, 0),
                           .spill_receiver = { 1, 4, 5, 6, 5, 8, 7, 8, 8 },
                           .outlets = { 8 } };
  const LakeCensus census = census_lakes (flood);

  MOPPE_CHECK (census.bodies.size () == 1);
  MOPPE_CHECK (census.bodies[0].outlet_cell == 4);
  MOPPE_CHECK (census.bodies[0].spill_cell == 5);
}

MOPPE_TEST (census_shape_separates_channel_water_from_lakes) {
  // Two permanent bodies of similar volume but different shape: a one-cell
  // wide flooded channel strip and a compact seven-by-seven lake. The shape
  // census must hand the strip to the river system and keep the lake.
  constexpr std::size_t width = 14;
  constexpr std::size_t height = 14;
  constexpr std::size_t count = width * height;
  const TerrainGrid grid { .width = width,
                           .height = height,
                           .spacing_x = 20.0f * mp_units::si::metre,
                           .spacing_y = 20.0f * mp_units::si::metre,
                           .height_scale = 10.0f * mp_units::si::metre };
  std::vector<float> depth (count, 0.0f);
  for (std::size_t x = 2; x <= 9; ++x)
    depth[2 * width + x] = 0.3f;
  for (std::size_t y = 5; y <= 11; ++y)
    for (std::size_t x = 3; x <= 9; ++x)
      depth[y * width + x] = 0.3f;
  std::vector<CellIndex> receiver (count, CellIndex { 0 });
  const FloodField flood {
    .source_grid = grid,
    .sea_level = 0.0f,
    .has_ocean = false,
    .water_level = ScalarRaster ({ .width = width, .height = height },
                                 std::vector<float> (count, 1.0f)),
    .water_depth = ScalarRaster ({ .width = width, .height = height }, depth),
    .ocean = std::vector<std::uint8_t> (count, 0),
    .spill_receiver = std::move (receiver),
    .outlets = { 0 }
  };
  const LakeCensus census = census_lakes (flood);

  MOPPE_CHECK (census.bodies.size () == 2);
  const WaterBody& strip = census.bodies[census.body[2 * width + 5]];
  const WaterBody& lake = census.bodies[census.body[8 * width + 6]];
  MOPPE_CHECK (water_body_is_permanent (strip));
  MOPPE_CHECK (water_body_is_permanent (lake));
  MOPPE_CHECK_NEAR (moppe::meters_value (strip.inradius), 20.0f, 0.0f);
  MOPPE_CHECK_NEAR (moppe::meters_value (lake.inradius), 80.0f, 0.0f);
  MOPPE_CHECK (strip.channel_like);
  MOPPE_CHECK (!lake.channel_like);
}

MOPPE_TEST (permanence_removes_small_ponds_but_never_the_sea) {
  const std::array heights { -2.f, -1.f, 1.f, 2.f };
  const TerrainView terrain ({ .width = 2, .height = 2 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);
  const ScalarRaster permanent = permanent_water_surface (
    flood,
    census,
    { .minimum_area = 1000000.0f * mp_units::si::metre * mp_units::si::metre });

  MOPPE_CHECK (census.bodies.size () == 1);
  MOPPE_CHECK (census.bodies[0].classification == WaterBodyClass::Sea);
  MOPPE_CHECK_NEAR (permanent.at (0, 0), 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (permanent.at (1, 0), 0.0f, 0.0f);
}
