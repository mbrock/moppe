#include <moppe/terrain/watercourse.hh>

#include <tests/test.hh>

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
    RiverNetwork rivers = extract_river_network (
      flood,
      census,
      drainage,
      100.0f * mp_units::si::metre * mp_units::si::metre);
    WaterSheets sheets =
      paint_watercourses (terrain, flood, census, drainage, rivers);
    return { std::move (heights), std::move (flood), std::move (census),
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

MOPPE_TEST (dry_ridges_remain_still) {
  const PaintedValley valley = paint_valley ();
  const std::size_t ridge = 0;
  MOPPE_CHECK_NEAR (
    valley.sheets.surface.values ()[ridge], valley.heights[ridge], 1e-6f);
  MOPPE_CHECK_NEAR (valley.sheets.flow[2 * ridge], 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (valley.sheets.flow[2 * ridge + 1], 0.0f, 0.0f);
}
