#ifndef MOPPE_RENDER_TYPES_HH
#define MOPPE_RENDER_TYPES_HH

#include <moppe/gfx/math.hh>

#include <cstdint>
#include <cstring>
#include <memory>

namespace moppe {
namespace render {
  // Primitive topologies accepted by the recording API.  Everything
  // is triangulated (or line-expanded) at record time; backends only
  // ever see triangles.
  enum class Prim {
    Triangles,
    TriangleStrip,
    TriangleFan,
    Quads,
    QuadStrip,
    Polygon,     // convex, fan-triangulated
    Lines,
    LineStrip
  };

  struct Color {
    uint8_t r, g, b, a;

    Color () : r (255), g (255), b (255), a (255) {}
    Color (float rf, float gf, float bf, float af = 1.0f)
      : r (quantize (rf)), g (quantize (gf)),
	b (quantize (bf)), a (quantize (af)) {}

    static uint8_t quantize (float x) {
      if (x <= 0) return 0;
      if (x >= 1) return 255;
      return (uint8_t) (x * 255.0f + 0.5f);
    }
  };

  // One streamed vertex; layout is mirrored by the shaders.
  struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    Color color;
    uint8_t lit;      // Lambert + specular vs. emissive passthrough
    uint8_t fogged;   // distance haze applied
    uint8_t pad0, pad1;
  };
  static_assert (sizeof (Vertex) == 40, "streamed vertex is 40 bytes");

  // Per-run fixed-function-ish state.  Kept tiny on purpose: it maps
  // to a handful of precompiled pipeline/depth-stencil objects.
  struct DrawState {
    bool blend;        // src-alpha / one-minus-src-alpha
    bool additive;     // src-alpha / one: glow that sums to white
    bool depth_test;
    bool depth_write;
    bool cull;         // back faces, CCW front

    DrawState ()
      : blend (false), additive (false), depth_test (true),
	depth_write (true), cull (true) {}

    bool operator == (const DrawState& o) const {
      return blend == o.blend && additive == o.additive
	&& depth_test == o.depth_test
	&& depth_write == o.depth_write && cull == o.cull;
    }
    bool operator != (const DrawState& o) const { return !(*this == o); }
  };

  enum class TextureFormat { RGBA8, RGB8 /* expanded on upload */ };
  enum class TextureFilter { Nearest, Linear, Mipmap };
  enum class TextureWrap { Repeat, Clamp };

  struct TextureDesc {
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    TextureFilter filter = TextureFilter::Linear;
    TextureWrap wrap = TextureWrap::Repeat;
    float max_anisotropy = 1.0f;
  };

  // Opaque backend resources.
  class Texture {
  public:
    virtual ~Texture () {}
    int width = 0;
    int height = 0;
  };

  class Mesh {
  public:
    virtual ~Mesh () {}
  };

  typedef std::shared_ptr<Texture> TexturePtr;
  typedef std::shared_ptr<Mesh> MeshPtr;
}
}

#endif
