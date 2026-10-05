#ifndef MOPPE_RENDER_TRUETYPE_HH
#define MOPPE_RENDER_TRUETYPE_HH

#include <moppe/render/slug.hh>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace moppe {
  namespace render {
    // A small reader for TrueType ('glyf') fonts: just what Slug text
    // needs.  It maps codepoints through cmap formats 4 and 12, reads
    // horizontal metrics, the legacy 'kern' pair table, and the OS/2
    // cap and x heights, and turns simple and composite glyph outlines into
    // closed quadratic contours.  TrueType outlines are already quadratic,
    // so nothing is approximated; implied on-curve midpoints between
    // consecutive off-curve points are made explicit.  Hinting, GSUB and
    // GPOS are ignored: Slug renders the true outline at any size, and the
    // HUD's short strings need no shaping beyond advances and pair kerning.
    //
    // All values are in font units with y up.  Throws std::runtime_error
    // for data that is not a TrueType font or is truncated.
    class TrueTypeFont {
    public:
      explicit TrueTypeFont (std::vector<std::uint8_t> bytes);

      static TrueTypeFont load (const std::string& path);

      int units_per_em () const {
        return m_units_per_em;
      }

      int ascender () const {
        return m_ascender;
      }

      int descender () const {
        return m_descender;
      }

      int line_gap () const {
        return m_line_gap;
      }

      // From OS/2 version 2 and later; zero when the font does not say.
      int cap_height () const {
        return m_cap_height;
      }

      int x_height () const {
        return m_x_height;
      }

      int glyph_count () const {
        return m_glyph_count;
      }

      // The glyph for a Unicode codepoint, or 0 (.notdef) when unmapped.
      std::uint16_t glyph_index (char32_t codepoint) const;

      int advance_width (std::uint16_t glyph) const;
      int left_side_bearing (std::uint16_t glyph) const;

      // Pair adjustment from a format-0 'kern' table; zero otherwise.
      int kerning (std::uint16_t left, std::uint16_t right) const;

      // Closed quadratic contours; empty for a blank glyph.
      std::vector<OutlineContour> outline (std::uint16_t glyph) const;

    private:
      struct Table {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
      };

      Table table (const char* tag) const;
      std::uint8_t u8 (std::size_t at) const;
      std::uint16_t u16 (std::size_t at) const;
      std::int16_t i16 (std::size_t at) const;
      std::uint32_t u32 (std::size_t at) const;
      bool glyph_range (std::uint16_t glyph,
                        std::uint32_t& begin,
                        std::uint32_t& end) const;
      void append_outline (std::uint16_t glyph,
                           const float transform[6],
                           int depth,
                           std::vector<OutlineContour>& out) const;

      std::vector<std::uint8_t> m_bytes;
      std::unordered_map<std::uint32_t, Table> m_tables;
      int m_units_per_em = 0;
      int m_ascender = 0;
      int m_descender = 0;
      int m_line_gap = 0;
      int m_cap_height = 0;
      int m_x_height = 0;
      int m_glyph_count = 0;
      int m_horizontal_metrics = 0;
      bool m_long_offsets = false;
      std::uint32_t m_cmap = 0; // absolute offset of the chosen subtable
      std::uint16_t m_cmap_format = 0;
      std::unordered_map<std::uint32_t, std::int16_t> m_kerning;
    };
  }
}

#endif
