#include <moppe/terrain/watercourse.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace moppe::terrain;

namespace {
  float sheet_level_m (const WaterSheets& sheets, std::size_t cell) {
    return surface_elevation_value (
      spatial::get<surface_elevation> (sheets)[cell]);
  }

  float sheet_wave (const WaterSheets& sheets, std::size_t cell) {
    return spatial::get<wave_amplitude> (sheets)[cell].numerical_value_in (one);
  }

  Vec3 sheet_velocity (const WaterSheets& sheets, std::size_t cell) {
    return spatial::get<water_velocity> (sheets)[cell].numerical_value_in (
      u::m / u::s);
  }

  const RiverReach* watercourse_reach_containing (const RiverNetwork& network,
                                                  CellIndex cell) {
    for (const RiverReach& reach : network.reaches)
      if (std::ranges::find (reach.cells, cell) != reach.cells.end ())
        return &reach;
    return nullptr;
  }

  std::vector<float> valley_to_sea () {
    std::vector<float> heights (9 * 9);
    for (int y = 0; y < 9; ++y)
      for (int x = 0; x < 9; ++x)
        heights[static_cast<std::size_t> (y) * 9 + x] =
          100.0f *
          (y == 8 ? -0.1f : 0.6f - 0.06f * y + 0.03f * std::abs (x - 4));
    return heights;
  }

  TerrainDomain valley_grid () {
    return { 9, 9, 5.0f * mp_units::si::metre, 5.0f * mp_units::si::metre };
  }

  struct PaintedValley {
    std::vector<float> heights;
    FloodField flood;
    LakeCensus census;
    DrainageGraph drainage;
    RiverNetwork rivers;
    WaterSheets sheets;
  };

  PaintedValley paint_valley () {
    std::vector<float> heights = valley_to_sea ();
    const ElevationMap terrain = make_elevation_map (valley_grid (), heights);
    FloodField flood = analyze_standing_water (terrain, 0.0f);
    LakeCensus census = census_lakes (flood);
    DrainageGraph drainage = analyze_wet_drainage (flood, census);
    RiverNetwork rivers = extract_river_network (flood,
                                                 census,
                                                 drainage,
                                                 100.0f * mp_units::si::metre *
                                                   mp_units::si::metre);
    WaterSheets sheets =
      paint_watercourses (terrain, flood, census, drainage, rivers);
    return { std::move (heights),  std::move (flood),  std::move (census),
             std::move (drainage), std::move (rivers), std::move (sheets) };
  }
}

MOPPE_TEST (running_rivers_are_shallow_flowing_water_in_the_shared_field) {
  const PaintedValley valley = paint_valley ();
  MOPPE_CHECK (!valley.rivers.reaches.empty ());

  bool found_running_cell = false;
  for (std::size_t cell = 0; cell < valley.heights.size (); ++cell) {
    if (valley.flood.water_depth_m (cell) > 0.0f)
      continue;
    const float depth =
      sheet_level_m (valley.sheets, cell) - valley.heights[cell];
    const Vec3 velocity = sheet_velocity (valley.sheets, cell);
    if (depth <= 0.005f || std::hypot (velocity[0], velocity[2]) <= 0.25f)
      continue;
    MOPPE_CHECK (depth <= 2.5f);
    MOPPE_CHECK_NEAR (sheet_wave (valley.sheets, cell), 0.0f, 0.0f);
    found_running_cell = true;
    break;
  }
  MOPPE_CHECK (found_running_cell);
}

MOPPE_TEST (standing_water_keeps_its_level_and_wave_character) {
  const PaintedValley valley = paint_valley ();
  const std::size_t sea = 8 * 9 + 1;

  MOPPE_CHECK (valley.flood.ocean[sea]);
  MOPPE_CHECK_NEAR (sheet_level_m (valley.sheets, sea),
                    valley.flood.water_level_m (sea),
                    1e-6f);
  MOPPE_CHECK_NEAR (sheet_wave (valley.sheets, sea), 1.0f, 0.0f);
}

MOPPE_TEST (river_current_continues_through_the_mouth) {
  const PaintedValley valley = paint_valley ();
  bool found_ocean_current = false;
  for (std::size_t cell = 0; cell < valley.flood.ocean.size (); ++cell) {
    if (!valley.flood.ocean[cell])
      continue;
    const Vec3 velocity = sheet_velocity (valley.sheets, cell);
    const float x = velocity[0];
    const float z = velocity[2];
    found_ocean_current = found_ocean_current || std::hypot (x, z) > 0.1f;
  }
  MOPPE_CHECK (found_ocean_current);
}

MOPPE_TEST (sill_between_terraced_bodies_signs_to_the_lower_level) {
  // Two flooded terraces stepping down toward the global-minimum drain at
  // the right: the upper basin fills to its 5 m sill, the lower to its 3 m
  // sill. The dry sill between them must carry the LOWER body's level so
  // the upper plate ends at its own waterline instead of extending across
  // the sill and overhanging the drop.
  constexpr std::size_t width = 9;
  constexpr std::size_t height = 3;
  const std::array<float, width> profile { 0.9f,  0.1f, 0.1f, 0.5f, 0.05f,
                                           0.05f, 0.3f, 0.0f, 0.9f };
  std::vector<float> heights (width * height);
  for (std::size_t y = 0; y < height; ++y)
    for (std::size_t x = 0; x < width; ++x)
      heights[y * width + x] = 10.0f * (y == 1 ? profile[x] : 0.9f);
  const TerrainDomain grid (
    width, height, 20.0f * mp_units::si::metre, 20.0f * mp_units::si::metre);
  const ElevationMap terrain = make_elevation_map (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, -10.0f);
  const LakeCensus census = census_lakes (flood);

  // Both terraces must be real, sheet-rendered bodies for the sill to
  // matter, and they must sit at distinct levels.
  std::size_t permanent_bodies = 0;
  for (const WaterBody& body : census.water_bodies ())
    permanent_bodies += water_body_is_permanent (body);
  MOPPE_CHECK (permanent_bodies == 2);
  const std::size_t upper = 1 * width + 1;
  const std::size_t sill = 1 * width + 3;
  const std::size_t lower = 1 * width + 4;
  MOPPE_CHECK_NEAR (flood.water_level_m (upper), 5.0f, 1e-5f);
  MOPPE_CHECK_NEAR (flood.water_level_m (lower), 3.0f, 1e-5f);

  const DrainageGraph drainage = analyze_wet_drainage (flood, census);
  const RiverNetwork rivers = extract_river_network (
    flood, census, drainage, 1e9f * mp_units::si::metre * mp_units::si::metre);
  const WaterSheets sheets =
    paint_watercourses (terrain, flood, census, drainage, rivers);

  MOPPE_CHECK_NEAR (sheet_level_m (sheets, upper), 5.0f, 1e-5f);
  MOPPE_CHECK_NEAR (sheet_level_m (sheets, lower), 3.0f, 1e-5f);
  MOPPE_CHECK_NEAR (sheet_level_m (sheets, sill), 3.0f, 1e-5f);
}

MOPPE_TEST (traversed_channel_like_bodies_join_the_running_water_field) {
  // A valley descending to the sea with a flooded channel stretch midway:
  // five cells of flat water one cell wide, permanent by size but shaped
  // like a river. The route must cross it as one continuous alignment that
  // pools at the flood level. The one water field keeps that level while its
  // wave amplitude and velocity identify the stretch as running water.
  constexpr std::size_t width = 14;
  constexpr std::size_t height = 3;
  const std::array<float, width> profile { 0.9f,  0.55f, 0.5f,  0.45f, 0.2f,
                                           0.2f,  0.2f,  0.2f,  0.2f,  0.25f,
                                           0.15f, 0.1f,  -0.1f, 0.9f };
  std::vector<float> heights (width * height);
  for (std::size_t y = 0; y < height; ++y)
    for (std::size_t x = 0; x < width; ++x)
      heights[y * width + x] = 100.0f * (y == 1 ? profile[x] : 0.9f);
  const TerrainDomain grid (
    width, height, 20.0f * mp_units::si::metre, 20.0f * mp_units::si::metre);
  const ElevationMap terrain = make_elevation_map (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);

  const WaterBodyId pond_id = census.body_at (CellIndex { 1 * width + 6 });
  MOPPE_CHECK (pond_id != LakeCensus::dry);
  const WaterBody& pond = census.water_body (pond_id);
  MOPPE_CHECK (water_body_is_permanent (pond));
  MOPPE_CHECK (pond.channel_like);
  MOPPE_CHECK (!water_body_terminates_rivers (pond));

  const DrainageGraph drainage = analyze_wet_drainage (flood, census);
  const RiverNetwork rivers =
    extract_river_network (flood,
                           census,
                           drainage,
                           1000.0f * mp_units::si::metre * mp_units::si::metre);

  MOPPE_CHECK (rivers.body_traversed.size () == census.domain ().size ());
  MOPPE_CHECK (rivers.body_traversed[pond_id]);

  // The inlet reach continues across the pond: it links downstream and its
  // alignment pools at the flood level.
  const RiverReach* inlet =
    watercourse_reach_containing (rivers, 1 * width + 3);
  MOPPE_CHECK (inlet);
  if (!inlet)
    return;
  MOPPE_CHECK (inlet->downstream_reach != RiverReach::no_id);
  bool pooled_point = false;
  for (const RiverAlignmentPoint& point : inlet->alignment.points)
    if (point.pooled > 0.9f) {
      pooled_point = true;
      MOPPE_CHECK (point.standing_water < 0.1f);
      MOPPE_CHECK_NEAR (point.water_level_m, 0.25f * 100.0f, 0.5f);
    }
  MOPPE_CHECK (pooled_point);

  // The shared sheet retains the pool's physical level and adds current,
  // while the sea keeps its standing plate.
  const WaterSheets sheets =
    paint_watercourses (terrain, flood, census, drainage, rivers);
  const std::size_t pond_cell = 1 * width + 6;
  MOPPE_CHECK_NEAR (
    sheet_level_m (sheets, pond_cell), flood.water_level_m (pond_cell), 1e-4f);
  MOPPE_CHECK_NEAR (sheet_wave (sheets, pond_cell), 0.0f, 0.0f);
  const Vec3 pond_velocity = sheet_velocity (sheets, pond_cell);
  MOPPE_CHECK (std::hypot (pond_velocity[0], pond_velocity[2]) > 0.25f);
  const std::size_t sea_cell = 1 * width + 12;
  MOPPE_CHECK_NEAR (sheet_level_m (sheets, sea_cell), 0.0f, 1e-5f);
}

MOPPE_TEST (dry_ridges_remain_still) {
  const PaintedValley valley = paint_valley ();
  // Row 0 borders the sea across the wrap and receives shore treatment;
  // probe an interior ridge cell instead.
  const std::size_t ridge = 3 * 9;
  MOPPE_CHECK_NEAR (
    sheet_level_m (valley.sheets, ridge), valley.heights[ridge], 1e-6f);
  const Vec3 velocity = sheet_velocity (valley.sheets, ridge);
  MOPPE_CHECK_NEAR (velocity[0], 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (velocity[2], 0.0f, 0.0f);
}
