#ifndef MOPPE_RENDER_TEXT_HH
#define MOPPE_RENDER_TEXT_HH

#include <moppe/render/types.hh>
#include <moppe/render/draw.hh>

#include <string>

namespace moppe {
namespace render {
  class Renderer;

  // Glyph atlas for one font at one size: the port of the GLUT
  // bitmap fonts.  At construction every ASCII glyph (32..126) is
  // rasterized through platform::rasterize_glyph at point_size x
  // scale (the display's backing scale, so text stays sharp on
  // Retina) and packed into a single RGBA8 texture.  Glyphs are
  // stored white with coverage in alpha, so the DrawList vertex
  // color tints the text exactly like glColor tinted glutBitmap
  // characters.
  //
  // draw() emits textured quads into the caller's DrawList; the
  // caller owns all state (the HUD sets blend on, depth off, cull
  // off, lit/fogged off).  (x, y) is the BASELINE origin in points,
  // y-down with the origin top-left, matching the glRasterPos
  // convention of the GL build closely enough that the old HUD
  // layout constants keep working.
  class FontAtlas {
  public:
    FontAtlas (Renderer& renderer, const char* family,
	       float point_size, float scale);

    // Draws text with the DrawList's current color; leaves the
    // list's texture unset (null) afterwards.  Returns the pen x
    // position after the last glyph.
    float draw (DrawList& dl, float x, float y,
		const std::string& text) const;

    // Width of the string in points.
    float measure (const std::string& text) const;

    float point_size () const { return m_point_size; }

    // False when the platform has no glyph rasterizer (draw and
    // measure become no-ops).
    bool ok () const { return (bool) m_texture; }

  private:
    enum {
      FIRST_CHAR = 32,
      LAST_CHAR = 126,
      GLYPH_COUNT = LAST_CHAR - FIRST_CHAR + 1
    };

    struct Glyph {
      float u0, v0, u1, v1;  // atlas rect
      float x_off;           // pen -> quad left edge, points
      float y_top;           // baseline up to quad top edge, points
      float w, h;            // quad size, points
      float advance;         // pen advance, points
      bool present;
    };

    Glyph m_glyphs[GLYPH_COUNT];
    TexturePtr m_texture;
    float m_point_size;
    float m_scale;
  };
}
}

#endif
