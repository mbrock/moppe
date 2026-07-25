#include <moppe/terrain/waterline.hh>

#include <tests/test.hh>

#include <array>
#include <vector>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  LakeCensus uniform_census (std::size_t count, WaterBodyId body) {
    LakeCensus census { .body =
                          std::vector<WaterBodyId> (count, LakeCensus::dry) };
    for (std::size_t i = 0; i < count; ++i)
      census.body[i] = body;
    return census;
  }

  ScalarRaster
  raster (std::size_t width, std::size_t height, std::vector<float> values) {
    return ScalarRaster ({ .width = width, .height = height },
                         std::move (values));
  }
}

MOPPE_TEST (waterline_finds_the_exact_bilinear_crossing) {
  // Ground climbs along x (0, 4, 8) out of a lake at level 1; the
  // sheet holds the level on the wet column and ground elsewhere, as
  // the painter does.  The level meets the ground slope a quarter of
  // the way along the first edge.
  const std::array ground { 0.f, 4.f, 8.f, 0.f, 4.f, 8.f, 0.f, 4.f, 8.f };
  const ElevationMap terrain = make_elevation_map (
    TerrainDomain (3, 3, 2.0f * mp_units::si::metre), ground);
  const ScalarRaster surface =
    raster (3, 3, { 1.f, 4.f, 8.f, 1.f, 4.f, 8.f, 1.f, 4.f, 8.f });
  const Waterline waterline = extract_waterline (
    terrain, surface, uniform_census (9, WaterBodyId { 4 }), 0.0f);

  // The wrap gives the wet column a second shoreline where the ground
  // falls from 8 back toward the water: level 1 meets that slope at
  // x = 5.75.  Both contours are straight lines of constant x.
  MOPPE_CHECK (waterline.contours.size () == 2);
  std::vector<float> shorelines;
  for (const WaterlineContour& contour : waterline.contours) {
    MOPPE_CHECK (contour.body == 4u);
    MOPPE_CHECK (contour.size () >= 3);
    for (std::size_t i = 1; i < contour.size (); ++i)
      MOPPE_CHECK_NEAR (contour.points[2 * i], contour.points[0], 1e-6f);
    shorelines.push_back (contour.points[0]);
  }
  std::ranges::sort (shorelines);
  MOPPE_CHECK_NEAR (shorelines[0], 0.5f, 1e-6f);
  MOPPE_CHECK_NEAR (shorelines[1], 5.75f, 1e-6f);
}

MOPPE_TEST (waterline_closes_a_loop_around_a_pond) {
  // A pond in a pit: ground dips to -2 at the center of a flat plain,
  // holding water at level -1.  The level crosses the pit's walls
  // halfway along each edge, so the contour is a closed diamond
  // through the four midpoints.
  std::vector<float> ground (25, 0.0f);
  ground[2 * 5 + 2] = -2.0f;
  std::vector<float> level = ground;
  level[2 * 5 + 2] = -1.0f;
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (5, 5), ground);
  LakeCensus census { .body = std::vector<WaterBodyId> (25, LakeCensus::dry) };
  census.body[2 * 5 + 2] = WaterBodyId { 7 };
  const Waterline waterline =
    extract_waterline (terrain, raster (5, 5, level), census, 0.0f);

  MOPPE_CHECK (waterline.contours.size () == 1);
  const WaterlineContour& contour = waterline.contours.front ();
  MOPPE_CHECK (contour.closed);
  MOPPE_CHECK (contour.body == 7u);
  MOPPE_CHECK (contour.size () == 4);
  // All four points half a cell from the wet node.
  for (std::size_t i = 0; i < contour.size (); ++i) {
    const float dx = contour.points[2 * i] - 2.0f;
    const float dy = contour.points[2 * i + 1] - 2.0f;
    MOPPE_CHECK_NEAR (std::abs (dx) + std::abs (dy), 0.5f, 1e-6f);
  }
}

MOPPE_TEST (waterline_wraps_around_the_torus) {
  // A wet column on a torus: two shorelines, each a loop closing
  // around the wrap.
  const std::size_t unique = 4;
  std::vector<float> ground (unique * unique, 0.0f);
  for (std::size_t y = 0; y < unique; ++y)
    ground[y * unique] = -2.0f;
  std::vector<float> level (unique * unique, 0.0f);
  for (std::size_t y = 0; y < unique; ++y)
    level[y * unique] = -1.0f;
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (unique, unique), ground);
  const Waterline waterline =
    extract_waterline (terrain,
                       raster (unique, unique, level),
                       uniform_census (unique * unique, WaterBodyId { 0 }),
                       0.0f);

  MOPPE_CHECK (waterline.contours.size () == 2);
  for (const WaterlineContour& contour : waterline.contours) {
    MOPPE_CHECK (contour.closed);
    MOPPE_CHECK (contour.size () == 4);
    // Each loop is a straight line of constant x.
    for (std::size_t i = 1; i < contour.size (); ++i)
      MOPPE_CHECK_NEAR (contour.points[2 * i], contour.points[0], 1e-6f);
  }
}

MOPPE_TEST (waterline_proximity_measures_the_band_exactly) {
  // The straight shoreline at x = 0.5 m (see the crossing test): node
  // distances are |x_world - 0.5|, clamped to the band.
  const std::array ground { 0.f, 4.f, 8.f, 0.f, 4.f, 8.f, 0.f, 4.f, 8.f };
  const ElevationMap terrain = make_elevation_map (
    TerrainDomain (3, 3, 2.0f * mp_units::si::metre), ground);
  const ScalarRaster surface =
    ScalarRaster ({ .width = 3, .height = 3 },
                  { 1.f, 4.f, 8.f, 1.f, 4.f, 8.f, 1.f, 4.f, 8.f });
  LakeCensus census { .body = std::vector<WaterBodyId> (9, WaterBodyId { 0 }) };
  const Waterline waterline =
    extract_waterline (terrain, surface, census, 0.0f);
  const WaterlineProximity proximity =
    waterline_proximity (waterline, 2.5f * u::m);
  const auto& distance = spatial::get<waterline_distance> (proximity);
  const auto at = [&] (std::size_t x, std::size_t y) {
    return distance[proximity.domain ().offset ({ x, y })].numerical_value_in (
      u::m);
  };

  // Nodes measure to the nearer of the two shorelines (x = 0.5 and,
  // across the wrap, x = 5.75).
  MOPPE_CHECK_NEAR (at (0, 1), 0.25f, 1e-5f);
  MOPPE_CHECK_NEAR (at (1, 1), 1.5f, 1e-5f);
  MOPPE_CHECK_NEAR (at (2, 1), 1.75f, 1e-5f);
}

MOPPE_TEST (waterline_extraction_is_deterministic) {
  std::vector<float> ground (49, 0.0f);
  std::vector<float> level (49);
  for (std::size_t y = 0; y < 7; ++y)
    for (std::size_t x = 0; x < 7; ++x)
      level[y * 7 + x] = 0.5f * std::sin (0.9f * static_cast<float> (x)) +
                         0.4f * std::cos (1.3f * static_cast<float> (y)) - 0.2f;
  const ElevationMap terrain =
    make_elevation_map (TerrainDomain (7, 7), ground);
  const LakeCensus census = uniform_census (49, WaterBodyId { 1 });
  const Waterline first =
    extract_waterline (terrain, raster (7, 7, level), census);
  const Waterline second =
    extract_waterline (terrain, raster (7, 7, level), census);

  MOPPE_CHECK (first.contours.size () == second.contours.size ());
  MOPPE_CHECK (!first.contours.empty ());
  for (std::size_t i = 0; i < first.contours.size (); ++i) {
    MOPPE_CHECK (first.contours[i].points == second.contours[i].points);
    MOPPE_CHECK (first.contours[i].closed == second.contours[i].closed);
  }
}
