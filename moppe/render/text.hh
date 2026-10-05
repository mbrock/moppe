#ifndef MOPPE_RENDER_TEXT_HH
#define MOPPE_RENDER_TEXT_HH

#include <moppe/render/slug.hh>
#include <moppe/render/truetype.hh>
#include <moppe/render/types.hh>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace moppe {
  namespace render {
    class Renderer;

    // Placed Slug glyphs awaiting a draw, grouped into runs that share one
    // uploaded glyph set.  It is the text counterpart of DrawList: the
    // caller records GlyphQuads in whatever target space the draw call
    // expects (HUD points for Renderer::draw_hud_text) and the backend
    // expands each into a dilated quad.
    class TextList {
    public:
      struct Run {
        GlyphSetPtr glyphs;
        std::uint32_t first;
        std::uint32_t count;
      };

      void clear () {
        m_quads.clear ();
        m_runs.clear ();
      }

      bool empty () const {
        return m_quads.empty ();
      }

      void add (const GlyphSetPtr& glyphs, const GlyphQuad& quad);

      // Moves every recorded quad, as DrawList::translate moves what
      // follows it; the HUD uses it to step inside the safe area.
      void translate (float dx, float dy);

      const std::vector<GlyphQuad>& quads () const {
        return m_quads;
      }

      const std::vector<Run>& runs () const {
        return m_runs;
      }

    private:
      std::vector<GlyphQuad> m_quads;
      std::vector<Run> m_runs;
    };

    struct TextStyle {
      float size = 16.0f; // points per em
      float red = 1.0f, green = 1.0f, blue = 1.0f, alpha = 1.0f;
      // Extra space after every glyph, in em: letterspacing for small
      // capitals and labels.
      float tracking = 0.0f;
      // The coverage filter's width in pixels.  One is exact, crisp
      // text; a wide filter draws the same outline softly, which is how
      // the HUD lays a gentle shadow beneath light text.
      float softness = 1.0f;
      // Give every digit the widest digit's advance so changing numbers
      // hold still.
      bool tabular_figures = false;
    };

    enum class TextAlign : std::uint8_t { Left, Center, Right };

    // A TrueType face prepared for Slug: the outlines of a fixed
    // repertoire are converted to band data once, at construction, and
    // uploaded as one glyph set.  Layout is single-line, by advance and
    // pair kerning, in HUD points with y down; glyph outlines keep their
    // y-up em frame and the quad's axes flip them onto the screen.
    class Font {
    public:
      // Prepares every codepoint of `repertoire` the face maps, plus
      // .notdef.  Without a renderer (tests) nothing is uploaded and
      // draw() records nothing.
      explicit Font (TrueTypeFont face,
                     std::u32string_view repertoire = latin_repertoire ());

      // Parses the font file at an asset-relative path and uploads it.
      static std::unique_ptr<Font> load (Renderer& renderer,
                                         const std::string& asset);

      // Printable ASCII, Latin-1, and the punctuation typographers reach
      // for: dashes, curly quotes, ellipsis, bullets, primes, arrows.
      static std::u32string_view latin_repertoire ();

      void upload (Renderer& renderer);

      bool ok () const {
        return (bool)m_glyph_set;
      }

      const TrueTypeFont& face () const {
        return m_face;
      }

      const SlugGlyphData& data () const {
        return m_builder.data ();
      }

      // The prepared glyph for a codepoint, or null when the face lacks it
      // (layout then falls back to .notdef).
      const SlugGlyph* glyph (char32_t codepoint) const;

      // Vertical metrics in points at a given size.
      float ascender (float size) const;
      float descender (float size) const;
      float cap_height (float size) const;
      float x_height (float size) const;

      float measure (std::string_view utf8, const TextStyle& style) const;

      // Records one line with its baseline at (x, y) in HUD points.  The
      // anchor is the line's left edge, centre, or right edge per `align`.
      // Returns the pen position after the last glyph.
      float draw (TextList& list,
                  float x,
                  float y,
                  std::string_view utf8,
                  const TextStyle& style,
                  TextAlign align = TextAlign::Left) const;

    private:
      struct Entry {
        SlugGlyph slug;
        std::uint16_t index = 0;
        float advance = 0.0f; // em
      };

      const Entry& entry (char32_t codepoint) const;
      float pen_advance (const Entry& entry,
                         char32_t codepoint,
                         const TextStyle& style) const;

      TrueTypeFont m_face;
      SlugAtlasBuilder m_builder;
      std::unordered_map<char32_t, Entry> m_entries;
      Entry m_notdef;
      float m_digit_advance = 0.0f; // em
      float m_pixels_per_point = 0.0f;
      GlyphSetPtr m_glyph_set;
    };

    // Decodes UTF-8, substituting U+FFFD for malformed sequences.
    std::u32string decode_utf8 (std::string_view utf8);
  }
}

#endif
