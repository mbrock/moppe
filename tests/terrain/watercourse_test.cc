#include <moppe/terrain/watercourse.hh>

#include <moppe/terrain/carve.hh>

#include <tests/test.hh>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace moppe::terrain;

namespace {
  // The carve tests' V-valley descending toward an ocean row: every
  // column drains into the center column, which carries a visible
  // river down to the sea.
  std::vector<float> watercourse_valley_to_sea () {
    std::vector<float> heights (9 * 9);
    for (int y = 0; y < 9; ++y)
      for (int x = 0; x < 9; ++x)
        heights[static_cast<std::size_t> (y) * 9 + x] =
          y == 8 ? -0.1f : 0.6f - 0.06f * y + 0.03f * std::abs (x - 4);
    return heights;
  }

  TerrainGrid watercourse_valley_grid () {
    return { .width = 9,
             .height = 9,
             .spacing_x = 5.0f * mp_units::si::metre,
             .spacing_y = 5.0f * mp_units::si::metre,
             .height_scale = 100.0f * mp_units::si::metre };
  }

  constexpr ChannelCarving watercourse_valley_parameters { .sea_level = 0.0f,
                                                           .minimum_area_cells =
                                                             4.0f };

  struct PaintedValley {
    std::vector<float> carved;
    FloodField flood;
    LakeCensus census;
    DrainageGraph drainage;
    RiverNetwork rivers;
    WaterSheets sheets;
  };

  PaintedValley paint_valley () {
    const std::vector<float> original = watercourse_valley_to_sea ();
    const TerrainView terrain (watercourse_valley_grid (), original);
    std::vector<float> carved =
      carve_channels (terrain, watercourse_valley_parameters).heights;
    const TerrainView carved_view (watercourse_valley_grid (), carved);
    FloodField flood = analyze_standing_water (
      carved_view, watercourse_valley_parameters.sea_level);
    LakeCensus census = census_lakes (flood);
    DrainageGraph drainage = analyze_wet_drainage (carved_view, flood, census);
    RiverNetwork rivers = extract_river_network (
      flood,
      census,
      drainage,
      watercourse_valley_parameters.minimum_area_cells * 25.0f *
        mp_units::si::metre * mp_units::si::metre);
    WaterSheets sheets =
      paint_watercourses (carved_view, flood, census, drainage, rivers);
    return { std::move (carved),   std::move (flood),  std::move (census),
             std::move (drainage), std::move (rivers), std::move (sheets) };
  }
}

MOPPE_TEST (painted_rivers_wet_the_trunk_at_the_shared_fill_law) {
  const PaintedValley valley = paint_valley ();
  MOPPE_CHECK (valley.rivers.reaches.size () >= 1);
  MOPPE_CHECK (valley.sheets.surface.values ().size () == 81);
  MOPPE_CHECK (valley.sheets.amplitude.size () == 81);
  MOPPE_CHECK (valley.sheets.flow.size () == 162);

  // Down the trunk the painted surface stands over the carved bed but
  // never deeper than the painting depth limit, and it never rises
  // downstream.
  const WatercoursePaint paint;
  float previous = std::numeric_limits<float>::infinity ();
  for (int y = 3; y <= 7; ++y) {
    const std::size_t cell = static_cast<std::size_t> (y) * 9 + 4;
    const float ground = valley.carved[cell] * 100.0f;
    const float level = valley.sheets.surface.values ()[cell] * 100.0f;
    MOPPE_CHECK (level > ground + 0.05f);
    MOPPE_CHECK (level <=
                 ground + moppe::meters_value (paint.depth_limit) + 1e-4f);
    MOPPE_CHECK (level <= previous + 1e-4f);
    previous = level;
  }
}

MOPPE_TEST (painted_flow_points_downstream_and_stays_in_the_speed_law) {
  const PaintedValley valley = paint_valley ();
  const WatercoursePaint paint;
  const float maximum = moppe::meters_per_second_value (
    paint.base_speed + paint.rapid_speed + paint.waterfall_speed);
  for (int y = 3; y <= 7; ++y) {
    const std::size_t cell = static_cast<std::size_t> (y) * 9 + 4;
    const float x = valley.sheets.flow[2 * cell];
    const float z = valley.sheets.flow[2 * cell + 1];
    // The valley drains toward the ocean row at the bottom.
    MOPPE_CHECK (z > 0.5f);
    MOPPE_CHECK (std::hypot (x, z) <= maximum + 1e-4f);
  }
}

MOPPE_TEST (painting_leaves_the_sea_and_the_dry_ridges_alone) {
  const PaintedValley valley = paint_valley ();
  // The ocean row keeps its flood level and its full swell.
  const std::size_t sea = 8 * 9 + 1;
  MOPPE_CHECK (valley.flood.ocean[sea]);
  MOPPE_CHECK_NEAR (valley.sheets.surface.values ()[sea],
                    valley.flood.water_level.values ()[sea],
                    1e-6f);
  MOPPE_CHECK_NEAR (valley.sheets.amplitude[sea], 1.0f, 0.0f);

  // A ridge corner stays dry ground with no arrow and no swell.
  const std::size_t ridge = 0;
  MOPPE_CHECK_NEAR (
    valley.sheets.surface.values ()[ridge], valley.carved[ridge], 1e-6f);
  MOPPE_CHECK_NEAR (valley.sheets.flow[2 * ridge], 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (valley.sheets.flow[2 * ridge + 1], 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (valley.sheets.amplitude[ridge], 0.0f, 0.0f);
}
