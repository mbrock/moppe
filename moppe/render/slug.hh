#ifndef MOPPE_RENDER_SLUG_HH
#define MOPPE_RENDER_SLUG_HH

#include <cstdint>
#include <span>
#include <vector>

namespace moppe {
  namespace render {
    // Glyphs and vector shapes drawn by Eric Lengyel's Slug method: the
    // fragment shader integrates coverage directly from the quadratic
    // outline, so text stays exact at every size and under any transform,
    // with no bitmap or distance field in between.  This header owns the
    // CPU half: outlines in em units, the per-glyph horizontal and vertical
    // bands that keep each pixel's curve walk short, and the dense buffers
    // the backend uploads.  moppe/shaders/metal/slug.metal is the GPU half,
    // and slug_coverage below mirrors it so tests can check the data
    // contract without a device.

    struct OutlinePoint {
      float x = 0.0f;
      float y = 0.0f;
    };

    // One quadratic Bézier segment.  A straight line is stored in Slug's
    // {p1, p2, p2} form, its control point repeated at the end.
    struct OutlineCurve {
      OutlinePoint p1;
      OutlinePoint p2;
      OutlinePoint p3;
    };

    inline OutlineCurve outline_line (OutlinePoint from, OutlinePoint to) {
      return { from, to, to };
    }

    // A closed contour: each curve ends where the next begins, and the last
    // ends where the first begins.  Orientation follows TrueType: filled
    // regions wind clockwise with y up, holes counterclockwise.  The nonzero
    // fill accepts either as long as holes oppose their outer contour.
    using OutlineContour = std::vector<OutlineCurve>;

    // One texel of the curve buffer.  A curve occupies one texel holding
    // p1 and p2; its p3 is the first half of the next texel, which is
    // either the following curve of the same contour (sharing the endpoint)
    // or the endpoint texel written after each contour.
    struct SlugCurveTexel {
      float x1 = 0.0f, y1 = 0.0f;
      float x2 = 0.0f, y2 = 0.0f;
    };

    // Where one glyph's data lives in a SlugGlyphData and the outline frame
    // its bands were cut in.  The shader selects bands from these exact
    // bounds; a quad drawn for the glyph may extend past them.
    struct SlugGlyph {
      float min_x = 0.0f, min_y = 0.0f;
      float max_x = 0.0f, max_y = 0.0f;
      std::uint32_t band_offset = 0;
      std::uint16_t horizontal_bands = 0;
      std::uint16_t vertical_bands = 0;

      bool empty () const {
        return horizontal_bands == 0 || vertical_bands == 0;
      }
    };

    // The uploaded data for a set of glyphs.  The band buffer holds, at
    // each glyph's band_offset, one (count, list offset) pair per
    // horizontal band and then per vertical band; each list is a run of
    // curve-texel indices.  Horizontal lists are sorted by descending
    // maximum x and vertical lists by descending maximum y, so a pixel can
    // stop walking once a curve lies wholly behind it.
    struct SlugGlyphData {
      std::vector<SlugCurveTexel> curves;
      std::vector<std::uint32_t> bands;
    };

    enum class SlugAxis : std::uint8_t { X, Y };

    // The reference's advice for band counts: the fewest bands in
    // [1, maximum] that minimise the most curves any single band holds.
    // Bands cut across `axis` (horizontal bands cut across y).
    int choose_slug_band_count (std::span<const OutlineCurve> curves,
                                SlugAxis axis,
                                float minimum,
                                float maximum,
                                int maximum_count = 16);

    class SlugAtlasBuilder {
    public:
      // Appends one outline, in em units, and returns where it landed.  An
      // outline without curves (a space) returns an empty glyph.  Throws
      // std::invalid_argument for a contour that is not closed.
      SlugGlyph add (std::span<const OutlineContour> contours);

      const SlugGlyphData& data () const {
        return m_data;
      }

    private:
      SlugGlyphData m_data;
    };

    // How far a band reaches past its edges when deciding membership, in
    // em, so a curve grazing a boundary belongs to both neighbours.
    inline constexpr float slug_band_epsilon = 1.0f / 1024.0f;

    // The fragment shader's coverage at an em-space sample, on the CPU.
    // pixels_per_em is the screen scale along each axis already divided by
    // the filter width; one pixel per filter width is ordinary crisp text.
    float slug_coverage (const SlugGlyphData& data,
                         const SlugGlyph& glyph,
                         OutlinePoint sample,
                         OutlinePoint pixels_per_em);
  }
}

#endif
