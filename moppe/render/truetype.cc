#include <moppe/render/truetype.hh>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace moppe {
  namespace render {
    namespace {
      std::uint32_t tag_value (const char* tag) {
        return (std::uint32_t (std::uint8_t (tag[0])) << 24) |
               (std::uint32_t (std::uint8_t (tag[1])) << 16) |
               (std::uint32_t (std::uint8_t (tag[2])) << 8) |
               std::uint32_t (std::uint8_t (tag[3]));
      }

      struct RawPoint {
        float x;
        float y;
        bool on_curve;
      };

      OutlinePoint midpoint (OutlinePoint a, OutlinePoint b) {
        return { 0.5f * (a.x + b.x), 0.5f * (a.y + b.y) };
      }

      // TrueType contours alternate on- and off-curve points, with an
      // implied on-curve point midway between two consecutive off-curve
      // ones.  Walk the ring from an on-curve start and emit one quadratic
      // per off-curve point and one line per on-curve pair.
      OutlineContour quadratic_contour (const std::vector<RawPoint>& ring) {
        OutlineContour contour;
        const std::size_t n = ring.size ();
        if (n < 2)
          return contour;
        const auto at = [&] (std::size_t i) {
          const RawPoint& p = ring[i % n];
          return OutlinePoint { p.x, p.y };
        };

        std::size_t first = n;
        for (std::size_t i = 0; i < n; ++i)
          if (ring[i].on_curve) {
            first = i;
            break;
          }
        // Every point off-curve: begin at the implied midpoint before p1.
        const std::size_t begin = first == n ? 0 : first;
        const OutlinePoint start =
          first == n ? midpoint (at (0), at (1)) : at (first);

        OutlinePoint pen = start;
        bool have_control = false;
        OutlinePoint control;
        for (std::size_t k = 1; k <= n; ++k) {
          const std::size_t i = begin + k;
          const bool on = ring[i % n].on_curve;
          const OutlinePoint p = at (i);
          if (on) {
            if (have_control)
              contour.push_back ({ pen, control, p });
            else if (p.x != pen.x || p.y != pen.y)
              contour.push_back (outline_line (pen, p));
            pen = p;
            have_control = false;
          } else if (have_control) {
            const OutlinePoint implied = midpoint (control, p);
            contour.push_back ({ pen, control, implied });
            pen = implied;
            control = p;
          } else {
            control = p;
            have_control = true;
          }
        }
        // A contour whose start was implied ends with a pending control
        // point that closes back to the start.
        if (have_control)
          contour.push_back ({ pen, control, start });
        return contour;
      }
    }

    TrueTypeFont TrueTypeFont::load (const std::string& path) {
      std::ifstream in (path, std::ios::binary);
      if (!in)
        throw std::runtime_error ("Cannot open font " + path);
      std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)),
                                       std::istreambuf_iterator<char> ());
      return TrueTypeFont (std::move (bytes));
    }

    std::uint8_t TrueTypeFont::u8 (std::size_t at) const {
      if (at + 1 > m_bytes.size ())
        throw std::runtime_error ("Truncated TrueType font");
      return m_bytes[at];
    }

    std::uint16_t TrueTypeFont::u16 (std::size_t at) const {
      if (at + 2 > m_bytes.size ())
        throw std::runtime_error ("Truncated TrueType font");
      return std::uint16_t ((m_bytes[at] << 8) | m_bytes[at + 1]);
    }

    std::int16_t TrueTypeFont::i16 (std::size_t at) const {
      return static_cast<std::int16_t> (u16 (at));
    }

    std::uint32_t TrueTypeFont::u32 (std::size_t at) const {
      return (std::uint32_t (u16 (at)) << 16) | u16 (at + 2);
    }

    TrueTypeFont::Table TrueTypeFont::table (const char* tag) const {
      const auto found = m_tables.find (tag_value (tag));
      return found == m_tables.end () ? Table {} : found->second;
    }

    TrueTypeFont::TrueTypeFont (std::vector<std::uint8_t> bytes)
        : m_bytes (std::move (bytes)) {
      const std::uint32_t version = u32 (0);
      if (version != 0x00010000u && version != tag_value ("true"))
        throw std::runtime_error ("Not a TrueType outline font");
      const int table_count = u16 (4);
      for (int i = 0; i < table_count; ++i) {
        const std::size_t record = 12 + 16 * i;
        const Table entry { u32 (record + 8), u32 (record + 12) };
        if (std::size_t (entry.offset) + entry.length > m_bytes.size ())
          throw std::runtime_error ("TrueType table runs past the file");
        m_tables[u32 (record)] = entry;
      }
      for (const char* required :
           { "head", "maxp", "hhea", "hmtx", "loca", "glyf", "cmap" })
        if (!table (required).length)
          throw std::runtime_error (std::string ("TrueType font lacks ") +
                                    required);

      const Table head = table ("head");
      m_units_per_em = u16 (head.offset + 18);
      m_long_offsets = i16 (head.offset + 50) != 0;
      if (m_units_per_em <= 0)
        throw std::runtime_error ("TrueType font has no units per em");
      m_glyph_count = u16 (table ("maxp").offset + 4);

      const Table hhea = table ("hhea");
      m_ascender = i16 (hhea.offset + 4);
      m_descender = i16 (hhea.offset + 6);
      m_line_gap = i16 (hhea.offset + 8);
      m_horizontal_metrics = u16 (hhea.offset + 34);

      const Table os2 = table ("OS/2");
      if (os2.length >= 90 && u16 (os2.offset) >= 2) {
        m_x_height = i16 (os2.offset + 86);
        m_cap_height = i16 (os2.offset + 88);
      }

      // Prefer a full-repertoire Unicode subtable, then the BMP one.
      const Table cmap = table ("cmap");
      const int subtables = u16 (cmap.offset + 2);
      int best_rank = 0;
      for (int i = 0; i < subtables; ++i) {
        const std::size_t record = cmap.offset + 4 + 8 * i;
        const int platform = u16 (record);
        const int encoding = u16 (record + 2);
        const std::uint32_t offset = cmap.offset + u32 (record + 4);
        const int format = u16 (offset);
        int rank = 0;
        if (format == 12 &&
            (platform == 0 || (platform == 3 && encoding == 10)))
          rank = 2;
        else if (format == 4 &&
                 (platform == 0 || (platform == 3 && encoding == 1)))
          rank = 1;
        if (rank > best_rank) {
          best_rank = rank;
          m_cmap = offset;
          m_cmap_format = format;
        }
      }
      if (!best_rank)
        throw std::runtime_error ("TrueType font has no Unicode cmap");

      // The legacy kern table: horizontal format-0 subtables only.
      const Table kern = table ("kern");
      if (kern.length >= 4 && u16 (kern.offset) == 0) {
        std::size_t at = kern.offset + 4;
        const int count = u16 (kern.offset + 2);
        for (int i = 0; i < count; ++i) {
          const std::size_t length = u16 (at + 2);
          const int coverage = u16 (at + 4);
          if ((coverage >> 8) == 0 && (coverage & 1) && !(coverage & 4)) {
            const int pairs = u16 (at + 6);
            for (int p = 0; p < pairs; ++p) {
              const std::size_t pair = at + 14 + 6 * p;
              m_kerning[u32 (pair)] = i16 (pair + 4);
            }
          }
          at += length;
        }
      }
    }

    std::uint16_t TrueTypeFont::glyph_index (char32_t codepoint) const {
      if (m_cmap_format == 12) {
        const std::uint32_t groups = u32 (m_cmap + 12);
        std::uint32_t low = 0, high = groups;
        while (low < high) {
          const std::uint32_t mid = (low + high) / 2;
          const std::size_t group = m_cmap + 16 + 12 * std::size_t (mid);
          const std::uint32_t first = u32 (group);
          const std::uint32_t last = u32 (group + 4);
          if (codepoint < first)
            high = mid;
          else if (codepoint > last)
            low = mid + 1;
          else
            return std::uint16_t (u32 (group + 8) + (codepoint - first));
        }
        return 0;
      }

      if (codepoint > 0xFFFF)
        return 0;
      const int segments = u16 (m_cmap + 6) / 2;
      const std::size_t ends = m_cmap + 14;
      const std::size_t starts = ends + 2 * segments + 2;
      const std::size_t deltas = starts + 2 * segments;
      const std::size_t ranges = deltas + 2 * segments;
      for (int s = 0; s < segments; ++s) {
        if (codepoint > u16 (ends + 2 * s))
          continue;
        const std::uint16_t start = u16 (starts + 2 * s);
        if (codepoint < start)
          return 0;
        const std::uint16_t delta = u16 (deltas + 2 * s);
        const std::uint16_t range = u16 (ranges + 2 * s);
        if (range == 0)
          return std::uint16_t (codepoint + delta);
        const std::size_t at =
          ranges + 2 * s + range + 2 * std::size_t (codepoint - start);
        const std::uint16_t glyph = u16 (at);
        return glyph ? std::uint16_t (glyph + delta) : 0;
      }
      return 0;
    }

    int TrueTypeFont::advance_width (std::uint16_t glyph) const {
      if (m_horizontal_metrics == 0)
        return 0;
      const int index = std::min<int> (glyph, m_horizontal_metrics - 1);
      return u16 (table ("hmtx").offset + 4 * index);
    }

    int TrueTypeFont::left_side_bearing (std::uint16_t glyph) const {
      const Table hmtx = table ("hmtx");
      if (glyph < m_horizontal_metrics)
        return i16 (hmtx.offset + 4 * glyph + 2);
      return i16 (hmtx.offset + 4 * m_horizontal_metrics +
                  2 * (glyph - m_horizontal_metrics));
    }

    int TrueTypeFont::kerning (std::uint16_t left, std::uint16_t right) const {
      const auto found =
        m_kerning.find ((std::uint32_t (left) << 16) | std::uint32_t (right));
      return found == m_kerning.end () ? 0 : found->second;
    }

    bool TrueTypeFont::glyph_range (std::uint16_t glyph,
                                    std::uint32_t& begin,
                                    std::uint32_t& end) const {
      if (glyph >= m_glyph_count)
        return false;
      const Table loca = table ("loca");
      if (m_long_offsets) {
        begin = u32 (loca.offset + 4 * glyph);
        end = u32 (loca.offset + 4 * glyph + 4);
      } else {
        begin = 2u * u16 (loca.offset + 2 * glyph);
        end = 2u * u16 (loca.offset + 2 * glyph + 2);
      }
      const Table glyf = table ("glyf");
      if (end <= begin || end > glyf.length)
        return false;
      begin += glyf.offset;
      end += glyf.offset;
      return true;
    }

    std::vector<OutlineContour>
    TrueTypeFont::outline (std::uint16_t glyph) const {
      std::vector<OutlineContour> contours;
      const float identity[6] = { 1, 0, 0, 1, 0, 0 };
      append_outline (glyph, identity, 0, contours);
      return contours;
    }

    void TrueTypeFont::append_outline (std::uint16_t glyph,
                                       const float m[6],
                                       int depth,
                                       std::vector<OutlineContour>& out) const {
      std::uint32_t begin = 0, end = 0;
      if (depth > 8 || !glyph_range (glyph, begin, end))
        return;

      const int contour_count = i16 (begin);
      if (contour_count >= 0) {
        std::size_t at = begin + 10;
        std::vector<int> contour_ends (contour_count);
        for (int i = 0; i < contour_count; ++i)
          contour_ends[i] = u16 (at + 2 * i);
        at += 2 * contour_count;
        const int point_count = contour_count ? contour_ends.back () + 1 : 0;
        at += 2 + u16 (at); // skip the hinting instructions

        std::vector<std::uint8_t> flags (point_count);
        for (int i = 0; i < point_count;) {
          const std::uint8_t flag = u8 (at++);
          flags[i++] = flag;
          if (flag & 0x08) {
            int repeat = u8 (at++);
            while (repeat-- > 0 && i < point_count)
              flags[i++] = flag;
          }
        }
        std::vector<RawPoint> points (point_count);
        int value = 0;
        for (int i = 0; i < point_count; ++i) {
          const std::uint8_t flag = flags[i];
          if (flag & 0x02) {
            value += (flag & 0x10) ? u8 (at) : -int (u8 (at));
            at += 1;
          } else if (!(flag & 0x10)) {
            value += i16 (at);
            at += 2;
          }
          points[i].x = float (value);
          points[i].on_curve = flag & 0x01;
        }
        value = 0;
        for (int i = 0; i < point_count; ++i) {
          const std::uint8_t flag = flags[i];
          if (flag & 0x04) {
            value += (flag & 0x20) ? u8 (at) : -int (u8 (at));
            at += 1;
          } else if (!(flag & 0x20)) {
            value += i16 (at);
            at += 2;
          }
          points[i].y = float (value);
        }
        for (RawPoint& p : points) {
          const float x = p.x, y = p.y;
          p.x = m[0] * x + m[2] * y + m[4];
          p.y = m[1] * x + m[3] * y + m[5];
        }

        // A reflecting component transform reverses winding; restore it so
        // composite holes still oppose their outer contours.
        const bool mirrored = m[0] * m[3] - m[1] * m[2] < 0.0f;
        int first = 0;
        for (int c = 0; c < contour_count; ++c) {
          std::vector<RawPoint> ring (points.begin () + first,
                                      points.begin () + contour_ends[c] + 1);
          first = contour_ends[c] + 1;
          if (mirrored)
            std::reverse (ring.begin (), ring.end ());
          OutlineContour contour = quadratic_contour (ring);
          if (!contour.empty ())
            out.push_back (std::move (contour));
        }
        return;
      }

      // Composite: each component is another glyph under a 2x2 transform
      // and an offset.  Point-matched placement (offsets naming points
      // rather than distances) is rare in modern fonts and is treated as
      // no offset.
      std::size_t at = begin + 10;
      for (;;) {
        const std::uint16_t flags = u16 (at);
        const std::uint16_t component = u16 (at + 2);
        at += 4;
        float dx = 0, dy = 0;
        if (flags & 0x0001) {
          if (flags & 0x0002) {
            dx = i16 (at);
            dy = i16 (at + 2);
          }
          at += 4;
        } else {
          if (flags & 0x0002) {
            dx = static_cast<std::int8_t> (u8 (at));
            dy = static_cast<std::int8_t> (u8 (at + 1));
          }
          at += 2;
        }
        const auto f2dot14 = [&] (std::size_t p) { return i16 (p) / 16384.0f; };
        float a = 1, b = 0, c = 0, d = 1;
        if (flags & 0x0008) {
          a = d = f2dot14 (at);
          at += 2;
        } else if (flags & 0x0040) {
          a = f2dot14 (at);
          d = f2dot14 (at + 2);
          at += 4;
        } else if (flags & 0x0080) {
          a = f2dot14 (at);
          b = f2dot14 (at + 2);
          c = f2dot14 (at + 4);
          d = f2dot14 (at + 6);
          at += 8;
        }
        // Compose: parent * component, with the component's offset placed
        // in the parent's frame.
        const float child[6] = {
          m[0] * a + m[2] * b,          m[1] * a + m[3] * b,
          m[0] * c + m[2] * d,          m[1] * c + m[3] * d,
          m[0] * dx + m[2] * dy + m[4], m[1] * dx + m[3] * dy + m[5],
        };
        append_outline (component, child, depth + 1, out);
        if (!(flags & 0x0020))
          break;
      }
    }
  }
}
