#include <moppe/render/slug.hh>
#include <tests/test.hh>

#include <cmath>
#include <stdexcept>
#include <vector>

using namespace moppe;
using render::OutlineContour;
using render::OutlinePoint;
using render::SlugAtlasBuilder;
using render::SlugGlyph;
using render::SlugGlyphData;

namespace {
  // An axis-aligned rectangle; TrueType winds filled regions clockwise
  // with y up and their holes counterclockwise.
  OutlineContour slug_rect (float x0, float y0, float x1, float y1, bool hole) {
    std::vector<OutlinePoint> corners = {
      { x0, y0 }, { x0, y1 }, { x1, y1 }, { x1, y0 }
    };
    if (hole)
      corners = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    OutlineContour contour;
    for (std::size_t i = 0; i < corners.size (); ++i)
      contour.push_back (
        render::outline_line (corners[i], corners[(i + 1) % corners.size ()]));
    return contour;
  }

  // A circle of equal quadratic arcs.
  OutlineContour slug_circle (float cx, float cy, float radius, int segments) {
    OutlineContour contour;
    const float step = 2.0f * 3.14159265f / segments;
    const float reach = radius / std::cos (0.5f * step);
    OutlinePoint at { cx + radius, cy };
    for (int i = 0; i < segments; ++i) {
      const float mid = (i + 0.5f) * step;
      const float end = (i + 1) * step;
      OutlinePoint to { cx + radius * std::cos (end),
                        cy + radius * std::sin (end) };
      if (i + 1 == segments)
        to = { cx + radius, cy };
      contour.push_back (
        { at,
          { cx + reach * std::cos (mid), cy + reach * std::sin (mid) },
          to });
      at = to;
    }
    return contour;
  }

  float slug_cov (const SlugGlyphData& data,
                  const SlugGlyph& glyph,
                  float x,
                  float y,
                  float pixels_per_em = 200.0f) {
    return render::slug_coverage (
      data, glyph, { x, y }, { pixels_per_em, pixels_per_em });
  }
}

MOPPE_TEST (slug_fills_an_outline_and_leaves_its_hole) {
  SlugAtlasBuilder builder;
  const std::vector<OutlineContour> frame = {
    slug_rect (0.0f, 0.0f, 1.0f, 1.0f, false),
    slug_rect (0.3f, 0.3f, 0.7f, 0.7f, true),
  };
  const SlugGlyph glyph = builder.add (frame);
  const SlugGlyphData& data = builder.data ();

  MOPPE_CHECK (!glyph.empty ());
  MOPPE_CHECK_NEAR (glyph.min_x, 0.0f, 0.0f);
  MOPPE_CHECK_NEAR (glyph.max_y, 1.0f, 0.0f);
  MOPPE_CHECK_NEAR (slug_cov (data, glyph, 0.15f, 0.5f), 1.0f, 1e-5f);
  MOPPE_CHECK_NEAR (slug_cov (data, glyph, 0.5f, 0.15f), 1.0f, 1e-5f);
  MOPPE_CHECK_NEAR (slug_cov (data, glyph, 0.5f, 0.5f), 0.0f, 1e-5f);
  MOPPE_CHECK_NEAR (slug_cov (data, glyph, 0.85f, 0.85f), 1.0f, 1e-5f);
  MOPPE_CHECK_NEAR (slug_cov (data, glyph, 1.2f, 0.5f), 0.0f, 1e-5f);
  MOPPE_CHECK_NEAR (slug_cov (data, glyph, -0.2f, 0.5f), 0.0f, 1e-5f);
}

MOPPE_TEST (slug_antialiases_across_one_pixel) {
  SlugAtlasBuilder builder;
  const std::vector<OutlineContour> square = { slug_rect (
    0.0f, 0.0f, 1.0f, 1.0f, false) };
  const SlugGlyph glyph = builder.add (square);
  const SlugGlyphData& data = builder.data ();

  // At 100 pixels per em a pixel is 0.01 em: coverage ramps linearly
  // from the pixel centre half a pixel inside to half a pixel outside.
  MOPPE_CHECK_NEAR (slug_cov (data, glyph, 1.0f, 0.5f, 100.0f), 0.5f, 1e-3f);
  MOPPE_CHECK_NEAR (
    slug_cov (data, glyph, 1.0f - 0.0025f, 0.5f, 100.0f), 0.75f, 1e-3f);
  MOPPE_CHECK_NEAR (
    slug_cov (data, glyph, 1.0f + 0.0025f, 0.5f, 100.0f), 0.25f, 1e-3f);
  MOPPE_CHECK_NEAR (slug_cov (data, glyph, 0.5f, 0.0f, 100.0f), 0.5f, 1e-3f);
  // A wider filter, as the HUD's shadows use, spreads the same edge.
  MOPPE_CHECK_NEAR (
    slug_cov (data, glyph, 1.0f + 0.01f, 0.5f, 25.0f), 0.25f, 1e-3f);
}

MOPPE_TEST (slug_follows_curved_edges) {
  SlugAtlasBuilder builder;
  const std::vector<OutlineContour> disc = { slug_circle (
    0.5f, 0.5f, 0.4f, 24) };
  const SlugGlyph glyph = builder.add (disc);
  const SlugGlyphData& data = builder.data ();

  for (int i = 0; i < 24; ++i) {
    const float angle = (i + 0.37f) * 2.0f * 3.14159265f / 24.0f;
    const float c = std::cos (angle), s = std::sin (angle);
    const auto at = [&] (float r) {
      return slug_cov (data, glyph, 0.5f + r * c, 0.5f + r * s, 400.0f);
    };
    MOPPE_CHECK_NEAR (at (0.38f), 1.0f, 1e-4f);
    MOPPE_CHECK_NEAR (at (0.42f), 0.0f, 1e-4f);
    // On the true circle the quadratic approximation is within a small
    // fraction of a pixel, so coverage sits near one half.
    MOPPE_CHECK_NEAR (at (0.4f), 0.5f, 0.12f);
  }
}

MOPPE_TEST (slug_bands_omit_parallel_lines_and_sort_by_reach) {
  SlugAtlasBuilder builder;
  const std::vector<OutlineContour> frame = {
    slug_rect (0.0f, 0.0f, 1.0f, 1.0f, false),
    slug_rect (0.3f, 0.3f, 0.7f, 0.7f, true),
  };
  const SlugGlyph glyph = builder.add (frame);
  const SlugGlyphData& data = builder.data ();

  const int bands = glyph.horizontal_bands + glyph.vertical_bands;
  for (int band = 0; band < bands; ++band) {
    const std::uint32_t count = data.bands[glyph.band_offset + 2 * band];
    const std::uint32_t list = data.bands[glyph.band_offset + 2 * band + 1];
    const bool horizontal = band < glyph.horizontal_bands;
    MOPPE_CHECK (count > 0);
    MOPPE_CHECK (list + count <= data.bands.size ());
    float previous = 1e9f;
    for (std::uint32_t i = 0; i < count; ++i) {
      const std::uint32_t texel = data.bands[list + i];
      MOPPE_CHECK (texel + 1 < data.curves.size ());
      const render::SlugCurveTexel& c = data.curves[texel];
      const render::SlugCurveTexel& n = data.curves[texel + 1];
      // Every curve here is a line; a horizontal band holds only those
      // that cross it vertically, and the converse.
      if (horizontal)
        MOPPE_CHECK (c.y1 != n.y1);
      else
        MOPPE_CHECK (c.x1 != n.x1);
      const float reach =
        horizontal ? std::max (c.x1, n.x1) : std::max (c.y1, n.y1);
      MOPPE_CHECK (reach <= previous);
      previous = reach;
    }
  }
}

MOPPE_TEST (slug_band_count_minimises_the_fullest_band) {
  // Ten thin, separate vertical bars: cutting x into ten columns gives each
  // column one bar, two lines; cutting y gains nothing over one band.
  std::vector<render::OutlineCurve> curves;
  for (int bar = 0; bar < 10; ++bar) {
    const OutlineContour contour =
      slug_rect (bar * 0.1f, 0.0f, bar * 0.1f + 0.05f, 1.0f, false);
    curves.insert (curves.end (), contour.begin (), contour.end ());
  }
  MOPPE_CHECK (render::choose_slug_band_count (
                 curves, render::SlugAxis::X, 0.0f, 0.95f) == 10);
  MOPPE_CHECK (render::choose_slug_band_count (
                 curves, render::SlugAxis::Y, 0.0f, 1.0f) == 1);
}

MOPPE_TEST (slug_shares_identical_band_lists) {
  SlugAtlasBuilder builder;
  const std::vector<OutlineContour> square = { slug_rect (
    0.0f, 0.0f, 1.0f, 1.0f, false) };
  const SlugGlyph glyph = builder.add (square);
  const SlugGlyphData& data = builder.data ();
  // A square has one band per axis whatever the count chosen; each
  // header's list is written once.
  const std::size_t headers =
    2 * (glyph.horizontal_bands + glyph.vertical_bands);
  MOPPE_CHECK (data.bands.size () <= headers + 4);
  MOPPE_CHECK (data.curves.size () == 5);
}

MOPPE_TEST (slug_rejects_open_contours_and_skips_blank_glyphs) {
  SlugAtlasBuilder builder;
  OutlineContour open = slug_rect (0.0f, 0.0f, 1.0f, 1.0f, false);
  open.pop_back ();
  const std::vector<OutlineContour> broken = { open };
  bool threw = false;
  try {
    builder.add (broken);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  const SlugGlyph blank = builder.add ({});
  MOPPE_CHECK (blank.empty ());
  MOPPE_CHECK_NEAR (
    render::slug_coverage (builder.data (), blank, { 0, 0 }, { 10, 10 }),
    0.0f,
    0.0f);
}
