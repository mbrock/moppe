#include <moppe/render/text.hh>
#include <moppe/render/truetype.hh>
#include <tests/test.hh>

#include <cmath>
#include <string>

using namespace moppe;
using render::Font;
using render::TrueTypeFont;

namespace {
  const TrueTypeFont& iosevka () {
    static const TrueTypeFont face = TrueTypeFont::load (
      std::string (MOPPE_TEST_SOURCE_DIR) + "/fonts/IosevkaAile-Regular.ttf");
    return face;
  }

  const Font& iosevka_font () {
    static const Font font (iosevka ());
    return font;
  }

  bool outline_closed (const std::vector<render::OutlineContour>& contours) {
    for (const render::OutlineContour& contour : contours)
      for (std::size_t i = 0; i < contour.size (); ++i) {
        const render::OutlinePoint end = contour[i].p3;
        const render::OutlinePoint next = contour[(i + 1) % contour.size ()].p1;
        if (end.x != next.x || end.y != next.y)
          return false;
      }
    return !contours.empty ();
  }
}

MOPPE_TEST (truetype_reads_the_bundled_face_metrics) {
  const TrueTypeFont& face = iosevka ();
  MOPPE_CHECK (face.units_per_em () == 1000);
  MOPPE_CHECK (face.cap_height () == 735);
  MOPPE_CHECK (face.x_height () == 520);
  MOPPE_CHECK (face.ascender () == 965);
  MOPPE_CHECK (face.descender () == -215);
  MOPPE_CHECK (face.glyph_count () > 1000);
  MOPPE_CHECK (face.glyph_index (U'A') != 0);
  MOPPE_CHECK (face.glyph_index (U'A') != face.glyph_index (U'B'));
  MOPPE_CHECK (face.glyph_index (U'\U0010FFFD') == 0);
  MOPPE_CHECK (face.advance_width (face.glyph_index (U'm')) > 0);
}

MOPPE_TEST (truetype_outlines_are_closed_quadratic_contours) {
  const TrueTypeFont& face = iosevka ();
  for (char32_t c : U"AOgå&@3%") {
    if (!c)
      continue;
    const auto contours = face.outline (face.glyph_index (c));
    MOPPE_CHECK (outline_closed (contours));
  }
  // O has a counter; i a dot; space is blank.
  MOPPE_CHECK (face.outline (face.glyph_index (U'O')).size () == 2);
  MOPPE_CHECK (face.outline (face.glyph_index (U'i')).size () == 2);
  MOPPE_CHECK (face.outline (face.glyph_index (U' ')).empty ());
  // An accented letter carries at least its base letter's contours,
  // whether the face composes it or draws it whole.
  MOPPE_CHECK (face.outline (face.glyph_index (U'å')).size () >
               face.outline (face.glyph_index (U'a')).size ());
}

MOPPE_TEST (font_glyph_coverage_matches_the_letterform) {
  const Font& font = iosevka_font ();
  const render::SlugGlyph* o = font.glyph (U'O');
  MOPPE_CHECK (o != nullptr);
  MOPPE_CHECK (!o->empty ());
  const float cx = 0.5f * (o->min_x + o->max_x);
  const float cy = 0.5f * (o->min_y + o->max_y);
  const render::OutlinePoint scale { 200.0f, 200.0f };
  // The counter is empty, the stroke solid, the outside clear.
  MOPPE_CHECK_NEAR (
    render::slug_coverage (font.data (), *o, { cx, cy }, scale), 0.0f, 1e-4f);
  const float stroke_x = o->min_x + 0.02f;
  MOPPE_CHECK_NEAR (
    render::slug_coverage (font.data (), *o, { stroke_x, cy }, scale),
    1.0f,
    1e-4f);
  MOPPE_CHECK_NEAR (
    render::slug_coverage (font.data (), *o, { o->max_x + 0.05f, cy }, scale),
    0.0f,
    1e-4f);
}

MOPPE_TEST (font_layout_advances_and_aligns) {
  const Font& font = iosevka_font ();
  render::TextStyle style;
  style.size = 20.0f;
  const float i = font.measure ("i", style);
  MOPPE_CHECK (i > 0.0f);
  MOPPE_CHECK_NEAR (font.measure ("iii", style), 3.0f * i, 1e-4f);
  // Tracking falls between glyphs, not after the last.
  render::TextStyle tracked = style;
  tracked.tracking = 0.1f;
  MOPPE_CHECK_NEAR (font.measure ("iii", tracked), 3.0f * i + 4.0f, 1e-4f);

  render::TextStyle tabular = style;
  tabular.tabular_figures = true;
  MOPPE_CHECK_NEAR (
    font.measure ("111", tabular), font.measure ("888", tabular), 1e-4f);
  MOPPE_CHECK_NEAR (font.cap_height (20.0f), 14.7f, 1e-4f);
  // Unknown characters fall back to .notdef rather than vanishing.
  MOPPE_CHECK (font.measure ("\xE2\x98\x83", style) > 0.0f);
}

MOPPE_TEST (font_draws_nothing_without_an_uploaded_glyph_set) {
  const Font& font = iosevka_font ();
  render::TextList list;
  render::TextStyle style;
  const float end = font.draw (list, 10.0f, 20.0f, "Ride", style);
  MOPPE_CHECK (list.empty ());
  MOPPE_CHECK_NEAR (end, 10.0f, 0.0f);
}

MOPPE_TEST (text_list_groups_runs_by_glyph_set) {
  render::TextList list;
  const render::GlyphSetPtr a = std::make_shared<render::GlyphSet> ();
  const render::GlyphSetPtr b = std::make_shared<render::GlyphSet> ();
  render::GlyphQuad quad {};
  list.add (a, quad);
  list.add (a, quad);
  list.add (b, quad);
  list.add (a, quad);
  MOPPE_CHECK (list.runs ().size () == 3);
  MOPPE_CHECK (list.runs ()[0].count == 2);
  MOPPE_CHECK (list.runs ()[2].first == 3);
  list.translate (5.0f, -2.0f);
  MOPPE_CHECK_NEAR (list.quads ()[3].origin[0], 5.0f, 0.0f);
  MOPPE_CHECK_NEAR (list.quads ()[3].origin[1], -2.0f, 0.0f);
}

MOPPE_TEST (utf8_decoding_handles_multibyte_and_malformed_input) {
  MOPPE_CHECK (render::decode_utf8 ("a\xC3\xA5\xE2\x80\x94") ==
               std::u32string (U"aå—"));
  MOPPE_CHECK (render::decode_utf8 ("\xF0\x9F\x8F\x8D") ==
               std::u32string (U"\U0001F3CD"));
  MOPPE_CHECK (render::decode_utf8 ("x\xC3") == std::u32string (U"x�"));
  MOPPE_CHECK (render::decode_utf8 ("\x80y") == std::u32string (U"�y"));
}
