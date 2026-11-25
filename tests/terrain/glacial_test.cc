#include <moppe/gfx/signal.hh>
#include <moppe/terrain/glacial.hh>
#include <moppe/terrain/readings.hh>
#include <moppe/terrain/river.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace moppe;
using namespace moppe::terrain;

MOPPE_TEST (glacial_trough_grades_main_stem_and_preserves_highland_shelf) {
  // A 150 m highland plateau dropping abruptly off a coastal cliff to a 50 m
  // ocean: a glacial trough must breach the escarpment down to sea level along
  // the main trunk while keeping the flanking highland shelves intact and
  // laying fresh alluvial outwash along the flat U-valley floor.
  constexpr int cols = 48;
  constexpr int rows = 56;
  constexpr int mid = cols / 2;
  constexpr int shore = 44;
  constexpr meters_t sea_m = 50.0f * u::m;
  const SurfaceElevation sea = surface_elevation_point (sea_m);
  const TerrainDomain grid (static_cast<std::size_t> (cols),
                            static_cast<std::size_t> (rows),
                            70.0f * u::m,
                            70.0f * u::m);
  std::vector<float> heights (static_cast<std::size_t> (cols) * rows);
  const auto idx = [&] (int r, int c) {
    return static_cast<std::size_t> (r) * cols + static_cast<std::size_t> (c);
  };
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      if (r >= shore) {
        const meters_t drop = static_cast<float> (r - shore) * 2.0f * u::m;
        heights[idx (r, c)] = meters_value (sea_m - 2.0f * u::m - drop);
      } else if (r == 0) {
        heights[idx (r, c)] = 210.0f;
      } else {
        // Elevated 145 m highland plateau sloping gently toward the center axis
        // with a sharp coastal escarpment at r = shore - 1.
        const meters_t fall = static_cast<float> (shore - r) * 0.35f * u::m;
        const meters_t side =
          static_cast<float> (std::abs (c - mid)) * 0.90f * u::m;
        heights[idx (r, c)] = meters_value (145.0f * u::m + fall + side);
      }
    }

  const ElevationMap terrain = make_elevation_map (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, sea);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph drainage = analyze_wet_drainage (flood, census);
  const RiverNetwork rivers = extract_river_network (
    flood, census, drainage, visible_river_minimum_area ());

  const GlacialSculptingResult glacial =
    sculpt_glacial_valleys (terrain, flood, census, drainage, rivers, sea);

  MOPPE_CHECK (glacial.troughs >= 1);
  // 1 km inland along the main trunk, the coastal escarpment cliff has been
  // breached down to a gentle navigable valley grade (~52-62 m vs ~150 m).
  const std::size_t inland = idx (shore - 12, mid);
  const meters_t inland_y =
    surface_elevation_value (glacial.heights[inland]) * u::m;
  MOPPE_CHECK (inland_y < sea_m + 18.0f * u::m);
  MOPPE_CHECK (inland_y > sea_m - 1.0f * u::m);
  MOPPE_CHECK (glacial.outwash[inland] > 0.5f * sediment_thickness[u::m]);

  // Far out on the flanking plateau (>500 m from the trough axis), the
  // elevated highland shelf stands preserved above 145 m.
  const std::size_t flank = idx (shore - 12, mid - 12);
  const meters_t flank_y =
    surface_elevation_value (glacial.heights[flank]) * u::m;
  MOPPE_CHECK (flank_y > 145.0f * u::m);
  MOPPE_CHECK (flank_y - inland_y > 75.0f * u::m);
}

MOPPE_TEST (alpine_cirques_carve_lee_bowls_without_lifting_peaks) {
  // A tall 260 m mountain dome rising above a 50 m sea: glacial sculpting must
  // bite an asymmetric leeward cirque bowl into its high flank without raising
  // any cell above the original surface.
  constexpr int size = 48;
  constexpr int mid = size / 2;
  constexpr meters_t sea_m = 50.0f * u::m;
  const SurfaceElevation sea = surface_elevation_point (sea_m);
  const TerrainDomain grid (static_cast<std::size_t> (size),
                            static_cast<std::size_t> (size),
                            25.0f * u::m,
                            25.0f * u::m);
  std::vector<float> heights (static_cast<std::size_t> (size) * size);
  for (int z = 0; z < size; ++z)
    for (int x = 0; x < size; ++x) {
      const meters_t dx = static_cast<float> (x - mid) * 25.0f * u::m;
      const meters_t dz = static_cast<float> (z - mid) * 25.0f * u::m;
      const meters_t r = hypot (plan_displacement (dx, dz));
      const proportion_t dome =
        1.0f * proportion[one] - band (0.0f * u::m, 520.0f * u::m, r);
      const meters_t h = sea_m - 4.0f * u::m + meters_t (220.0f * u::m * dome);
      heights[static_cast<std::size_t> (z) * size + x] = meters_value (h);
    }

  const ElevationMap terrain = make_elevation_map (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, sea);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph drainage = analyze_wet_drainage (flood, census);
  const RiverNetwork rivers = extract_river_network (
    flood, census, drainage, visible_river_minimum_area ());

  GlacialSculpting params;
  params.vidda_above_sea = 500.0f * u::m;
  const GlacialSculptingResult glacial = sculpt_glacial_valleys (
    terrain, flood, census, drainage, rivers, sea, params);

  MOPPE_CHECK (glacial.cirques >= 1);
  meters_t max_cut = 0.0f * u::m;
  for (std::size_t i = 0; i < heights.size (); ++i) {
    const meters_t before = heights[i] * u::m;
    const meters_t after = surface_elevation_value (glacial.heights[i]) * u::m;
    MOPPE_CHECK (after <= before + 1e-4f * u::m);
    max_cut = std::max (max_cut, before - after);
  }
  MOPPE_CHECK (max_cut > 12.0f * u::m);
}

MOPPE_TEST (
  kalfjall_snowfield_creates_broad_high_plateau_and_snow_basin_cover) {
  // A broad alpine range rising to 290 m above a 50 m sea: glacial snowfield
  // sculpting must form a wide, gently rolling high snow plateau (>240 m),
  // lay snow-basin drift cover across it, and never raise the world's highest
  // summit past its natural cap.
  constexpr int size = 64;
  constexpr int mid = size / 2;
  constexpr meters_t spacing = 50.0f * u::m;
  constexpr meters_t sea_m = 50.0f * u::m;
  const SurfaceElevation sea = surface_elevation_point (sea_m);
  const TerrainDomain grid (static_cast<std::size_t> (size),
                            static_cast<std::size_t> (size),
                            spacing,
                            spacing);
  std::vector<float> heights (static_cast<std::size_t> (size) * size);
  for (int z = 0; z < size; ++z)
    for (int x = 0; x < size; ++x) {
      const meters_t dx = static_cast<float> (x - mid) * spacing;
      const meters_t dz = static_cast<float> (z - mid) * spacing;
      const meters_t r = hypot (plan_displacement (dx, dz));
      const proportion_t massif =
        1.0f * proportion[one] - band (0.0f * u::m, 1250.0f * u::m, r);
      const float ridge = 22.0f * std::sin (static_cast<float> (x) * 0.45f) *
                          std::cos (static_cast<float> (z) * 0.45f);
      const meters_t h = sea_m - 6.0f * u::m +
                         meters_t (245.0f * u::m * massif) +
                         ridge * massif.numerical_value_in (one) * u::m;
      heights[static_cast<std::size_t> (z) * size + x] = meters_value (h);
    }

  const ElevationMap terrain = make_elevation_map (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, sea);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph drainage = analyze_wet_drainage (flood, census);
  const RiverNetwork rivers = extract_river_network (
    flood, census, drainage, visible_river_minimum_area ());

  GlacialSculpting params;
  params.snowfield_radius_along = 520.0f * u::m;
  params.snowfield_radius_across = 380.0f * u::m;
  params.snow_valley_length = 720.0f * u::m;
  params.snow_valley_half_width = 65.0f * u::m;
  const GlacialSculptingResult glacial = sculpt_glacial_valleys (
    terrain, flood, census, drainage, rivers, sea, params);

  MOPPE_CHECK (glacial.snowfields == 1);
  std::size_t high_covered = 0;
  for (std::size_t i = 0; i < heights.size (); ++i) {
    if (glacial.heights[i] >= sea + 185.0f * u::m &&
        glacial.outwash[i] > 1.0f * sediment_thickness[u::m])
      ++high_covered;
  }
  // Broad rideable snowfield + descending snow valley must cover a substantial
  // area (> 100 cells = > 0.25 km² on this 3.2 km test grid).
  MOPPE_CHECK (high_covered > 100);

  // Re-analysing standing water after glacial sculpting must not trap a chain
  // of ponded depressions along the snowfield plateau or descending chute.
  const ElevationMap post = make_elevation_map (grid, [&] {
    std::vector<float> h (heights.size ());
    for (std::size_t i = 0; i < heights.size (); ++i)
      h[i] = surface_elevation_value (glacial.heights[i]);
    return h;
  }());
  const FloodField post_flood = analyze_standing_water (post, sea);
  std::size_t ponded_high = 0;
  for (std::size_t i = 0; i < heights.size (); ++i)
    if (glacial.heights[i] >= sea + 140.0f * u::m &&
        post_flood.water_depths ()[i] > 0.25f * u::m)
      ++ponded_high;
  MOPPE_CHECK (ponded_high < 15);
}

MOPPE_TEST (alpine_horns_ring_snowfield_without_skewing_climatic_relief) {
  // A broad 4.8 km alpine massif surrounded by lowland foothills: glacial
  // sculpting should ring the inland rim of the snowfield with dramatic jagged
  // alpine horn peaks (> +80 m above the snowfield), keep the snowfield center
  // rideable, and leave climatic relief anchored near the massif plateau.
  constexpr int size = 96;
  constexpr int mid = size / 2;
  constexpr meters_t spacing = 50.0f * u::m;
  constexpr meters_t sea_m = 50.0f * u::m;
  const SurfaceElevation sea = surface_elevation_point (sea_m);
  const TerrainDomain grid (static_cast<std::size_t> (size),
                            static_cast<std::size_t> (size),
                            spacing,
                            spacing);
  std::vector<float> heights (static_cast<std::size_t> (size) * size);
  for (int z = 0; z < size; ++z)
    for (int x = 0; x < size; ++x) {
      const meters_t dx = static_cast<float> (x - mid) * spacing;
      const meters_t dz = static_cast<float> (z - mid) * spacing;
      const meters_t r = hypot (plan_displacement (dx, dz));
      const proportion_t massif =
        1.0f * proportion[one] - band (0.0f * u::m, 1850.0f * u::m, r);
      const proportion_t inland = band (-1400.0f * u::m, 1400.0f * u::m, dx);
      const meters_t h = sea_m + 14.0f * u::m +
                         meters_t (235.0f * u::m * massif) +
                         meters_t (35.0f * u::m * inland);
      heights[static_cast<std::size_t> (z) * size + x] = meters_value (h);
    }

  const ElevationMap terrain = make_elevation_map (grid, heights);
  const FloodField flood = analyze_standing_water (terrain, sea);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph drainage = analyze_wet_drainage (flood, census);
  const RiverNetwork rivers = extract_river_network (
    flood, census, drainage, visible_river_minimum_area ());

  GlacialSculpting params;
  params.snowfield_radius_along = 560.0f * u::m;
  params.snowfield_radius_across = 420.0f * u::m;
  params.snow_valley_length = 720.0f * u::m;
  params.snow_valley_half_width = 65.0f * u::m;
  const GlacialSculptingResult glacial = sculpt_glacial_valleys (
    terrain, flood, census, drainage, rivers, sea, params);

  MOPPE_CHECK (glacial.snowfields == 1);
  MOPPE_CHECK (glacial.horns >= 7);

  SurfaceElevation max_horn = sea;
  for (const SurfaceElevation y : glacial.heights)
    max_horn = std::max (max_horn, y);
  const SurfaceElevation center_y =
    glacial.heights[static_cast<std::size_t> (mid) * size + mid];
  // Towering alpine horns rise substantially above the snowfield floor.
  MOPPE_CHECK (max_horn - center_y > 85.0f * u::m);
  // Climatic relief stays anchored near the massif plateau rather than jumping
  // all the way up to the narrow summit spires.
  const meters_t climatic =
    terrain::detail::measure_climatic_relief (grid, glacial.heights, sea);
  const meters_t raw_max = quantity_cast<isq::length> (max_horn - sea);
  MOPPE_CHECK (climatic < raw_max - 35.0f * u::m);
  MOPPE_CHECK (climatic > 180.0f * u::m);
}
