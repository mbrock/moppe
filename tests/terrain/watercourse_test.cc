#include <moppe/terrain/watercourse.hh>

#include <tests/test.hh>

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace moppe::terrain;

namespace {
  std::vector<float> valley_to_sea () {
    std::vector<float> heights (9 * 9);
    for (int y = 0; y < 9; ++y)
      for (int x = 0; x < 9; ++x)
        heights[static_cast<std::size_t> (y) * 9 + x] =
          y == 8 ? -0.1f : 0.6f - 0.06f * y + 0.03f * std::abs (x - 4);
    return heights;
  }

  TerrainGrid valley_grid () {
    return { .width = 9,
             .height = 9,
             .spacing_x = 5.0f * mp_units::si::metre,
             .spacing_y = 5.0f * mp_units::si::metre,
             .height_scale = 100.0f * mp_units::si::metre };
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
    const TerrainView terrain (valley_grid (), heights);
    FloodField flood = analyze_standing_water (terrain, 0.0f);
    LakeCensus census = census_lakes (flood);
    DrainageGraph drainage = analyze_wet_drainage (terrain, flood, census);
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

MOPPE_TEST (running_rivers_do_not_mutate_the_standing_water_lattice) {
  const PaintedValley valley = paint_valley ();
  MOPPE_CHECK (!valley.rivers.reaches.empty ());

  const std::size_t dry_trunk = 4 * 9 + 4;
  MOPPE_CHECK (valley.flood.water_depth.values ()[dry_trunk] == 0.0f);
  MOPPE_CHECK_NEAR (valley.sheets.surface.values ()[dry_trunk],
                    valley.heights[dry_trunk],
                    1e-6f);
  MOPPE_CHECK_NEAR (valley.sheets.amplitude[dry_trunk], 0.0f, 0.0f);
}

MOPPE_TEST (standing_water_keeps_its_level_and_wave_character) {
  const PaintedValley valley = paint_valley ();
  const std::size_t sea = 8 * 9 + 1;

  MOPPE_CHECK (valley.flood.ocean[sea]);
  MOPPE_CHECK_NEAR (valley.sheets.surface.values ()[sea],
                    valley.flood.water_level.values ()[sea],
                    1e-6f);
  MOPPE_CHECK_NEAR (valley.sheets.amplitude[sea], 1.0f, 0.0f);
}

MOPPE_TEST (river_current_continues_through_the_mouth) {
  const PaintedValley valley = paint_valley ();
  bool found_ocean_current = false;
  for (std::size_t cell = 0; cell < valley.flood.ocean.size (); ++cell) {
    if (!valley.flood.ocean[cell])
      continue;
    const float x = valley.sheets.flow[2 * cell];
    const float z = valley.sheets.flow[2 * cell + 1];
    found_ocean_current = found_ocean_current || std::hypot (x, z) > 0.1f;
  }
  MOPPE_CHECK (found_ocean_current);
}

MOPPE_TEST (sill_between_terraced_bodies_signs_to_the_lower_level) {
  // Two flooded terraces stepping down toward the global-minimum drain at
  // the right: the upper basin fills to its 0.5 sill, the lower to its 0.3
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
      heights[y * width + x] = y == 1 ? profile[x] : 0.9f;
  const TerrainGrid grid { .width = width,
                           .height = height,
                           .spacing_x = 20.0f * mp_units::si::metre,
                           .spacing_y = 20.0f * mp_units::si::metre,
                           .height_scale = 10.0f * mp_units::si::metre };
  const TerrainView terrain (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, -1.0f);
  const LakeCensus census = census_lakes (flood);

  // Both terraces must be real, sheet-rendered bodies for the sill to
  // matter, and they must sit at distinct levels.
  std::size_t permanent_bodies = 0;
  for (const WaterBody& body : census.bodies)
    permanent_bodies += water_body_is_permanent (body);
  MOPPE_CHECK (permanent_bodies == 2);
  const std::size_t upper = 1 * width + 1;
  const std::size_t sill = 1 * width + 3;
  const std::size_t lower = 1 * width + 4;
  MOPPE_CHECK_NEAR (flood.water_level.values ()[upper], 0.5f, 1e-5f);
  MOPPE_CHECK_NEAR (flood.water_level.values ()[lower], 0.3f, 1e-5f);

  const DrainageGraph drainage = analyze_wet_drainage (terrain, flood, census);
  const RiverNetwork rivers = extract_river_network (
    flood, census, drainage, 1e9f * mp_units::si::metre * mp_units::si::metre);
  const WaterSheets sheets =
    paint_watercourses (terrain, flood, census, drainage, rivers);

  MOPPE_CHECK_NEAR (sheets.surface.values ()[upper], 0.5f, 1e-5f);
  MOPPE_CHECK_NEAR (sheets.surface.values ()[lower], 0.3f, 1e-5f);
  MOPPE_CHECK_NEAR (sheets.surface.values ()[sill], 0.3f, 1e-5f);
}

MOPPE_TEST (rivers_own_traversed_channel_like_bodies) {
  // A valley descending to the sea with a flooded channel stretch midway:
  // five cells of flat water one cell wide, permanent by size but shaped
  // like a river. The route must cross it as one continuous alignment that
  // pools at the flood level, and the standing-water sheet must yield the
  // stretch to the ribbon.
  constexpr std::size_t width = 14;
  constexpr std::size_t height = 3;
  const std::array<float, width> profile { 0.9f,  0.55f, 0.5f,  0.45f, 0.2f,
                                           0.2f,  0.2f,  0.2f,  0.2f,  0.25f,
                                           0.15f, 0.1f,  -0.1f, 0.9f };
  std::vector<float> heights (width * height);
  for (std::size_t y = 0; y < height; ++y)
    for (std::size_t x = 0; x < width; ++x)
      heights[y * width + x] = y == 1 ? profile[x] : 0.9f;
  const TerrainGrid grid { .width = width,
                           .height = height,
                           .spacing_x = 20.0f * mp_units::si::metre,
                           .spacing_y = 20.0f * mp_units::si::metre,
                           .height_scale = 100.0f * mp_units::si::metre };
  const TerrainView terrain (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);

  const WaterBodyId pond_id = census.body[1 * width + 6];
  MOPPE_CHECK (pond_id != LakeCensus::dry);
  const WaterBody& pond = census.bodies[pond_id];
  MOPPE_CHECK (water_body_is_permanent (pond));
  MOPPE_CHECK (pond.channel_like);
  MOPPE_CHECK (!water_body_terminates_rivers (pond));

  const DrainageGraph drainage = analyze_wet_drainage (terrain, flood, census);
  const RiverNetwork rivers =
    extract_river_network (flood,
                           census,
                           drainage,
                           1000.0f * mp_units::si::metre * mp_units::si::metre);

  MOPPE_CHECK (rivers.body_traversed.size () == census.bodies.size ());
  MOPPE_CHECK (rivers.body_traversed[pond_id]);

  // The inlet reach continues across the pond: it links downstream and its
  // alignment pools at the flood level with the ribbon staying opaque.
  const RiverReachId inlet_id = rivers.reach_by_cell[1 * width + 3];
  MOPPE_CHECK (inlet_id != RiverReach::no_id);
  const RiverReach& inlet = rivers.reaches[inlet_id];
  MOPPE_CHECK (inlet.downstream_reach != RiverReach::no_id);
  bool pooled_point = false;
  for (const RiverAlignmentPoint& point : inlet.alignment.points)
    if (point.pooled > 0.9f) {
      pooled_point = true;
      MOPPE_CHECK (point.standing_water < 0.1f);
      MOPPE_CHECK_NEAR (point.water_level_m, 0.25f * 100.0f, 0.5f);
    }
  MOPPE_CHECK (pooled_point);

  // The sheet yields the traversed stretch: its cells drop to ground level
  // and carry no wave amplitude, while the sea keeps its plate.
  const WaterSheets sheets =
    paint_watercourses (terrain, flood, census, drainage, rivers);
  const std::size_t pond_cell = 1 * width + 6;
  MOPPE_CHECK_NEAR (
    sheets.surface.values ()[pond_cell], heights[pond_cell], 1e-4f);
  MOPPE_CHECK_NEAR (sheets.amplitude[pond_cell], 0.0f, 0.0f);
  const std::size_t sea_cell = 1 * width + 12;
  MOPPE_CHECK_NEAR (sheets.surface.values ()[sea_cell], 0.0f, 1e-5f);
}

MOPPE_TEST (dry_ridges_remain_still) {
  const PaintedValley valley = paint_valley ();
  const std::size_t ridge = 0;
  MOPPE_CHECK_NEAR (
    valley.sheets.surface.values ()[ridge], valley.heights[ridge], 1e-6f);
  MOPPE_CHECK_NEAR (valley.sheets.flow[2 * ridge], 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (valley.sheets.flow[2 * ridge + 1], 0.0f, 0.0f);
}
