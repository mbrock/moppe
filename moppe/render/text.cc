#include <moppe/platform/platform.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <vector>

namespace moppe {
  namespace render {
    namespace {
      // Shelf-pack the glyph bitmaps left-to-right into rows of the
      // given atlas width (2 px spacing against linear-filter bleed).
      // Optionally records each glyph's position; returns the packed
      // height, or 0 when nothing needed pixels.
      int shelf_pack (const platform::GlyphBitmap* bitmaps,
                      const bool* ok,
                      int count,
                      int atlas_w,
                      int* out_x,
                      int* out_y) {
        const int spacing = 2;
        int pen_x = spacing;
        int pen_y = spacing;
        int row_h = 0;
        bool any = false;

        for (int i = 0; i < count; ++i) {
          if (out_x) {
            out_x[i] = -1;
            out_y[i] = -1;
          }
          if (!ok[i] || bitmaps[i].width <= 0 || bitmaps[i].height <= 0)
            continue;

          const int w = bitmaps[i].width;
          const int h = bitmaps[i].height;
          if (pen_x + w + spacing > atlas_w && pen_x > spacing) {
            pen_y += row_h + spacing;
            pen_x = spacing;
            row_h = 0;
          }
          if (out_x) {
            out_x[i] = pen_x;
            out_y[i] = pen_y;
          }
          if (h > row_h)
            row_h = h;
          pen_x += w + spacing;
          any = true;
        }
        return any ? pen_y + row_h + spacing : 0;
      }
    }

    FontAtlas::FontAtlas (Renderer& renderer,
                          const char* family,
                          float point_size,
                          float scale)
        : m_point_size (point_size), m_scale (scale > 0.0f ? scale : 1.0f) {
      platform::GlyphBitmap bitmaps[GLYPH_COUNT];
      bool ok[GLYPH_COUNT];

      for (int i = 0; i < GLYPH_COUNT; ++i)
        ok[i] = platform::rasterize_glyph (family,
                                           point_size,
                                           m_scale,
                                           (unsigned int)(FIRST_CHAR + i),
                                           bitmaps[i]);

      // Widen the atlas until every glyph fits on a shelf and the
      // packing is roughly square.
      int max_w = 0;
      for (int i = 0; i < GLYPH_COUNT; ++i)
        if (ok[i] && bitmaps[i].width > max_w)
          max_w = bitmaps[i].width;

      int atlas_w = 128;
      while (atlas_w < max_w + 2)
        atlas_w *= 2;
      while (shelf_pack (bitmaps, ok, GLYPH_COUNT, atlas_w, 0, 0) > atlas_w &&
             atlas_w < 2048)
        atlas_w *= 2;

      int xs[GLYPH_COUNT], ys[GLYPH_COUNT];
      const int atlas_h =
        shelf_pack (bitmaps, ok, GLYPH_COUNT, atlas_w, xs, ys);

      for (int i = 0; i < GLYPH_COUNT; ++i) {
        Glyph& g = m_glyphs[i];
        g = Glyph ();
        g.present = ok[i];
        if (!ok[i])
          continue;

        const platform::GlyphBitmap& bm = bitmaps[i];
        g.advance = bm.advance;
        if (xs[i] < 0)
          continue; // blank glyph (space): advance only

        g.x_off = bm.bearing_x / m_scale;
        g.y_top = bm.bearing_y / m_scale;
        g.w = bm.width / m_scale;
        g.h = bm.height / m_scale;
        g.u0 = (float)xs[i] / atlas_w;
        g.v0 = (float)ys[i] / atlas_h;
        g.u1 = (float)(xs[i] + bm.width) / atlas_w;
        g.v1 = (float)(ys[i] + bm.height) / atlas_h;
      }

      if (atlas_h <= 0)
        return; // no rasterizer on this platform

      // White glyphs, coverage in alpha.
      std::vector<unsigned char> pixels ((size_t)atlas_w * atlas_h * 4, 0);
      for (int i = 0; i < GLYPH_COUNT; ++i) {
        if (!ok[i] || xs[i] < 0)
          continue;
        const platform::GlyphBitmap& bm = bitmaps[i];
        for (int y = 0; y < bm.height; ++y) {
          for (int x = 0; x < bm.width; ++x) {
            const unsigned char c = bm.pixels[(size_t)y * bm.width + x];
            unsigned char* dst =
              &pixels[((size_t)(ys[i] + y) * atlas_w + (xs[i] + x)) * 4];
            dst[0] = dst[1] = dst[2] = 255;
            dst[3] = c;
          }
        }
      }

      TextureDesc desc;
      desc.width = atlas_w;
      desc.height = atlas_h;
      desc.format = TextureFormat::RGBA8;
      desc.filter = TextureFilter::Linear;
      desc.wrap = TextureWrap::Clamp;
      m_texture = renderer.create_texture (desc, &pixels[0]);
    }

    float FontAtlas::draw (DrawList& dl,
                           float x,
                           float y,
                           const std::string& text) const {
      float pen = x;
      if (!m_texture)
        return pen;

      dl.set_texture (m_texture.get ());
      dl.begin (Prim::Quads);
      for (size_t i = 0; i < text.size (); ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < FIRST_CHAR || c > LAST_CHAR)
          continue;
        const Glyph& g = m_glyphs[c - FIRST_CHAR];
        if (!g.present)
          continue;

        if (g.w > 0 && g.h > 0) {
          const float x0 = pen + g.x_off;
          const float y0 = y - g.y_top;
          const float x1 = x0 + g.w;
          const float y1 = y0 + g.h;
          dl.uv (g.u0, g.v0);
          dl.vertex (x0, y0);
          dl.uv (g.u1, g.v0);
          dl.vertex (x1, y0);
          dl.uv (g.u1, g.v1);
          dl.vertex (x1, y1);
          dl.uv (g.u0, g.v1);
          dl.vertex (x0, y1);
        }
        pen += g.advance;
      }
      dl.end ();
      dl.set_texture (0);
      dl.uv (0, 0);
      return pen;
    }

    float FontAtlas::measure (const std::string& text) const {
      float w = 0;
      for (size_t i = 0; i < text.size (); ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < FIRST_CHAR || c > LAST_CHAR)
          continue;
        const Glyph& g = m_glyphs[c - FIRST_CHAR];
        if (g.present)
          w += g.advance;
      }
      return w;
    }
  }
}
