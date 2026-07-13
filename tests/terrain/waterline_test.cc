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
  // Flat ground at zero; the water sheet ramps down along x, so the
  // signed field at nodes x = 0, 1, 2 is +1, -3, -7: one straight
  // shoreline crossing the bottom edges at s = 1 / (1 + 3) = 0.25.
  const std::array ground { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
  const TerrainView terrain (
    { .width = 3, .height = 3, .spacing_x = 2.0f * mp_units::si::metre },
    ground);
  const ScalarRaster surface =
    raster (3, 3, { 1.f, -3.f, -7.f, 1.f, -3.f, -7.f, 1.f, -3.f, -7.f });
  const Waterline waterline = extract_waterline (
    terrain, surface, uniform_census (9, WaterBodyId { 4 }), 0.0f);

  MOPPE_CHECK (waterline.contours.size () == 1);
  const WaterlineContour& contour = waterline.contours.front ();
  MOPPE_CHECK (!contour.closed);
  MOPPE_CHECK (contour.body == 4u);
  MOPPE_CHECK (contour.size () == 3);
  // Every point sits at x = 0.25 cells * 2 m spacing.
  for (std::size_t i = 0; i < contour.size (); ++i)
    MOPPE_CHECK_NEAR (contour.points[2 * i], 0.5f, 1e-6f);
  // Chained in order along the shoreline.
  MOPPE_CHECK_NEAR (contour.points[1], 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (contour.points[3], 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (contour.points[5], 2.0f, 1e-6f);
}

MOPPE_TEST (waterline_closes_a_loop_around_a_pond) {
  // One wet node in the middle of a dry 5x5 plain: the contour is a
  // closed diamond through the four midpoints around it.
  std::vector<float> ground (25, 0.0f);
  std::vector<float> level (25, -1.0f);
  level[2 * 5 + 2] = 1.0f;
  const TerrainView terrain ({ .width = 5, .height = 5 }, ground);
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
  // around the periodic seam.  The stored grid duplicates the seam, so
  // width 5 means 4 unique columns.
  const std::size_t unique = 4;
  std::vector<float> ground (25, 0.0f);
  std::vector<float> level (unique * unique, -1.0f);
  for (std::size_t y = 0; y < unique; ++y)
    level[y * unique] = 1.0f;
  const TerrainView terrain (
    { .width = 5, .height = 5, .topology = Topology::Torus }, ground);
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

MOPPE_TEST (waterline_extraction_is_deterministic) {
  std::vector<float> ground (49, 0.0f);
  std::vector<float> level (49);
  for (std::size_t y = 0; y < 7; ++y)
    for (std::size_t x = 0; x < 7; ++x)
      level[y * 7 + x] = 0.5f * std::sin (0.9f * static_cast<float> (x)) +
                         0.4f * std::cos (1.3f * static_cast<float> (y)) - 0.2f;
  const TerrainView terrain ({ .width = 7, .height = 7 }, ground);
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
