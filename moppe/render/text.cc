#include <moppe/platform/platform.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace render {
    void TextList::add (const GlyphSetPtr& glyphs, const GlyphQuad& quad) {
      if (m_runs.empty () || m_runs.back ().glyphs != glyphs)
        m_runs.push_back (
          { glyphs, static_cast<std::uint32_t> (m_quads.size ()), 0 });
      m_quads.push_back (quad);
      ++m_runs.back ().count;
    }

    void TextList::translate (float dx, float dy) {
      for (GlyphQuad& quad : m_quads) {
        quad.origin[0] += dx;
        quad.origin[1] += dy;
      }
    }

    std::u32string decode_utf8 (std::string_view utf8) {
      std::u32string out;
      out.reserve (utf8.size ());
      for (std::size_t i = 0; i < utf8.size ();) {
        const unsigned char lead = utf8[i];
        int length = lead < 0x80           ? 1
                     : (lead >> 5) == 0x06 ? 2
                     : (lead >> 4) == 0x0E ? 3
                     : (lead >> 3) == 0x1E ? 4
                                           : 0;
        char32_t codepoint = length == 1   ? lead
                             : length == 2 ? (lead & 0x1F)
                             : length == 3 ? (lead & 0x0F)
                                           : (lead & 0x07);
        bool valid = length > 0 && i + length <= utf8.size ();
        for (int k = 1; valid && k < length; ++k) {
          const unsigned char next = utf8[i + k];
          valid = (next & 0xC0) == 0x80;
          codepoint = (codepoint << 6) | (next & 0x3F);
        }
        out.push_back (valid ? codepoint : U'�');
        i += valid ? length : 1;
      }
      return out;
    }

    std::u32string_view Font::latin_repertoire () {
      static const std::u32string repertoire = [] {
        std::u32string chars;
        for (char32_t c = 0x20; c < 0x7F; ++c)
          chars.push_back (c);
        for (char32_t c = 0xA0; c <= 0xFF; ++c)
          chars.push_back (c);
        for (char32_t c : U"  –—‘’“”"
                          U"•…′″‹›←↑"
                          U"→↓−·�")
          chars.push_back (c);
        return chars;
      }();
      return repertoire;
    }

    Font::Font (TrueTypeFont face, std::u32string_view repertoire)
        : m_face (std::move (face)) {
      const float em = 1.0f / m_face.units_per_em ();
      const auto prepare = [&] (std::uint16_t index) {
        std::vector<OutlineContour> contours = m_face.outline (index);
        for (OutlineContour& contour : contours)
          for (OutlineCurve& curve : contour)
            for (OutlinePoint* p : { &curve.p1, &curve.p2, &curve.p3 }) {
              p->x *= em;
              p->y *= em;
            }
        Entry entry;
        entry.index = index;
        entry.advance = m_face.advance_width (index) * em;
        entry.slug = m_builder.add (contours);
        return entry;
      };

      m_notdef = prepare (0);
      // Several codepoints often share one glyph (no-break space and
      // space); prepare each glyph once.
      std::unordered_map<std::uint16_t, Entry> by_index;
      for (char32_t codepoint : repertoire) {
        const std::uint16_t index = m_face.glyph_index (codepoint);
        if (index == 0)
          continue;
        auto found = by_index.find (index);
        if (found == by_index.end ())
          found = by_index.emplace (index, prepare (index)).first;
        m_entries[codepoint] = found->second;
      }
      for (char32_t digit = U'0'; digit <= U'9'; ++digit)
        m_digit_advance = std::max (m_digit_advance, entry (digit).advance);
    }

    std::unique_ptr<Font> Font::load (Renderer& renderer,
                                      const std::string& asset) {
      auto font = std::make_unique<Font> (
        TrueTypeFont::load (platform::asset_path (asset)));
      font->upload (renderer);
      return font;
    }

    void Font::upload (Renderer& renderer) {
      m_glyph_set = renderer.create_glyph_set (m_builder.data ());
      m_pixels_per_point = renderer.scale_factor ();
    }

    const Font::Entry& Font::entry (char32_t codepoint) const {
      const auto found = m_entries.find (codepoint);
      return found == m_entries.end () ? m_notdef : found->second;
    }

    const SlugGlyph* Font::glyph (char32_t codepoint) const {
      const auto found = m_entries.find (codepoint);
      return found == m_entries.end () ? nullptr : &found->second.slug;
    }

    float Font::ascender (float size) const {
      return size * m_face.ascender () / m_face.units_per_em ();
    }

    float Font::descender (float size) const {
      return size * m_face.descender () / m_face.units_per_em ();
    }

    float Font::cap_height (float size) const {
      const int cap = m_face.cap_height ();
      return cap ? size * cap / m_face.units_per_em () : 0.7f * size;
    }

    float Font::x_height (float size) const {
      const int x = m_face.x_height ();
      return x ? size * x / m_face.units_per_em () : 0.5f * size;
    }

    float Font::pen_advance (const Entry& entry,
                             char32_t codepoint,
                             const TextStyle& style) const {
      const bool digit = codepoint >= U'0' && codepoint <= U'9';
      const float advance =
        style.tabular_figures && digit ? m_digit_advance : entry.advance;
      return (advance + style.tracking) * style.size;
    }

    float Font::measure (std::string_view utf8, const TextStyle& style) const {
      const std::u32string text = decode_utf8 (utf8);
      const float em = style.size / m_face.units_per_em ();
      float width = 0.0f;
      const Entry* previous = nullptr;
      for (char32_t codepoint : text) {
        const Entry& current = entry (codepoint);
        if (previous)
          width += m_face.kerning (previous->index, current.index) * em;
        width += pen_advance (current, codepoint, style);
        previous = &current;
      }
      // Tracking belongs between glyphs, not after the last one.
      if (!text.empty ())
        width -= style.tracking * style.size;
      return width;
    }

    float Font::draw (TextList& list,
                      float x,
                      float y,
                      std::string_view utf8,
                      const TextStyle& style,
                      TextAlign align) const {
      if (!m_glyph_set)
        return x;
      if (align != TextAlign::Left) {
        const float width = measure (utf8, style);
        x -= align == TextAlign::Center ? 0.5f * width : width;
      }
      // Slug needs no hinting, but a baseline and starting edge on whole
      // device pixels keep horizontal stems and the line's left edge from
      // straddling two pixel rows.
      if (m_pixels_per_point > 0.0f) {
        x = std::round (x * m_pixels_per_point) / m_pixels_per_point;
        y = std::round (y * m_pixels_per_point) / m_pixels_per_point;
      }

      const std::u32string text = decode_utf8 (utf8);
      const float em = style.size / m_face.units_per_em ();
      const Entry* previous = nullptr;
      for (char32_t codepoint : text) {
        const Entry& current = entry (codepoint);
        if (previous)
          x += m_face.kerning (previous->index, current.index) * em;
        float offset = 0.0f;
        const bool digit = codepoint >= U'0' && codepoint <= U'9';
        if (style.tabular_figures && digit)
          offset = 0.5f * (m_digit_advance - current.advance) * style.size;
        if (!current.slug.empty ()) {
          const SlugGlyph& g = current.slug;
          GlyphQuad quad {};
          quad.origin[0] = x + offset;
          quad.origin[1] = y;
          quad.origin[3] = style.softness;
          quad.axis_x[0] = style.size;
          quad.axis_y[1] = -style.size; // font y is up, the HUD's is down
          quad.bounds[0] = g.min_x;
          quad.bounds[1] = g.min_y;
          quad.bounds[2] = g.max_x;
          quad.bounds[3] = g.max_y;
          quad.color[0] = style.red;
          quad.color[1] = style.green;
          quad.color[2] = style.blue;
          quad.color[3] = style.alpha;
          quad.glyph[0] = g.band_offset;
          quad.glyph[1] = g.horizontal_bands;
          quad.glyph[2] = g.vertical_bands;
          list.add (m_glyph_set, quad);
        }
        x += pen_advance (current, codepoint, style);
        previous = &current;
      }
      return text.empty () ? x : x - style.tracking * style.size;
    }
  }
}
