#include <moppe/game/foliage.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/render/renderer.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace moppe::game {
  namespace {
    using namespace mp_units;

    // Display-space greens, decoded to linear by the scene shader. The two
    // species differ in more than hue: a conifer holds its needles in deep
    // shaded columns, a broadleaf spreads a thinner brighter sheet.
    constexpr Vec3 broadleaf_sun (0.185f, 0.350f, 0.095f);
    constexpr Vec3 broadleaf_shade (0.052f, 0.142f, 0.065f);
    constexpr Vec3 conifer_sun (0.118f, 0.270f, 0.095f);
    constexpr Vec3 conifer_shade (0.034f, 0.108f, 0.064f);

    void foliage_vertex (render::DrawList& draw,
                         const FoliagePalette& palette,
                         const FoliageVertex& vertex) {
      const Vec3 colour =
        linear_vector_interpolate (palette.shaded,
                                   palette.sunlit,
                                   std::clamp (vertex.exposure, 0.0f, 1.0f));
      draw.color (colour[0], colour[1], colour[2]);
      draw.normal (normalized (vertex.outward));
      draw.wind (vertex.bend);
      draw.flutter (vertex.flutter);
      draw.uv (vertex.u, vertex.v);
      draw.vertex (vertex.point);
    }
  }

  FoliagePalette
  crown_palette (FoliageKind kind, FoliageGround ground, std::uint32_t seed) {
    const bool conifer = kind == FoliageKind::conifer;
    // Where this individual falls on the stand's olive-to-blue-green spread,
    // and the occasional pale one -- a birch among spruce -- without which a
    // whole hillside reads as one painted mass.
    const float olive = hash_lane (seed, 19) - 0.5f;
    const float pallor = smoothstep (0.84f, 1.0f, hash_lane (seed, 23));
    const float vigour = 0.80f + 0.32f * ground.moisture + 0.16f * ground.cover;
    const Vec3 tint (1.0f + 0.66f * olive + 0.72f * pallor,
                     vigour + 0.30f * pallor,
                     1.0f - 0.70f * olive + 0.28f * pallor);
    return { scaled (conifer ? conifer_sun : broadleaf_sun, tint),
             scaled (conifer ? conifer_shade : broadleaf_shade, tint) };
  }

  FoliagePalette bark_palette (FoliageKind kind, std::uint32_t seed) {
    // Young bark keeps its species colour; old bark greys out whatever it
    // started as, which is most of what tells two neighbours apart.
    const Vec3 fresh = kind == FoliageKind::conifer
                         ? Vec3 (0.245f, 0.132f, 0.076f)
                         : Vec3 (0.220f, 0.166f, 0.122f);
    const Vec3 lit = linear_vector_interpolate (
      fresh, Vec3 (0.168f, 0.152f, 0.136f), hash_lane (seed, 11));
    // Bark in shade is not black. It is lit by the ground and by whatever
    // gets through the crown, and treating it as an unlit silhouette is what
    // makes a stand look like fence posts driven into a lawn.
    return { lit, lit * 0.52f };
  }

  render::TexturePtr make_leaf_card_texture (render::Renderer& renderer) {
    constexpr int size = 128;
    constexpr int leaf_count = 27;
    struct Leaf {
      float x;
      float y;
      float width;
      float height;
      float turn;
    };

    std::array<Leaf, leaf_count> leaves;
    for (int index = 0; index < leaf_count; ++index) {
      const float angle = PI2 * hash_lane (0x6c656166U, 3 * index);
      const float distance =
        0.76f * std::sqrt (hash_lane (0x6c656166U, 3 * index + 1));
      leaves[index] = {
        .x = distance * std::cos (angle),
        .y = 0.90f * distance * std::sin (angle),
        .width = 0.13f + 0.075f * hash_lane (0x6c656166U, 3 * index + 2),
        .height = 0.20f + 0.10f * hash_lane (0x6c656166U, 3 * index + 7),
        .turn = angle + 0.65f * (hash_lane (0x6c656166U, 3 * index + 11) - 0.5f)
      };
    }

    std::vector<unsigned char> pixels (size * size * 4, 255);
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const float px = 2.0f * (x + 0.5f) / size - 1.0f;
        const float py = 2.0f * (y + 0.5f) / size - 1.0f;
        float coverage = 0.0f;
        for (const Leaf& leaf : leaves) {
          const float cosine = std::cos (leaf.turn);
          const float sine = std::sin (leaf.turn);
          const float dx = px - leaf.x;
          const float dy = py - leaf.y;
          const float across = (cosine * dx + sine * dy) / leaf.width;
          const float along = (-sine * dx + cosine * dy) / leaf.height;
          // A sub-quadratic superellipse tapers to recognisable leaf tips
          // without putting high-frequency serration into the mip chain.
          const float shape = std::pow (std::abs (across), 1.55f) +
                              std::pow (std::abs (along), 1.18f);
          coverage = std::max (coverage, smoothstep (1.05f, 0.90f, shape));
        }
        pixels[(y * size + x) * 4 + 3] = static_cast<unsigned char> (
          std::clamp (std::lround (255.0f * coverage), 0l, 255l));
      }

    render::TextureDesc description;
    description.width = size;
    description.height = size;
    description.format = render::TextureFormat::RGBA8;
    description.filter = render::TextureFilter::Mipmap;
    description.wrap = render::TextureWrap::Clamp;
    description.max_anisotropy = 4.0f;
    return renderer.create_texture (description, pixels.data ());
  }

  render::TexturePtr make_conifer_card_texture (render::Renderer& renderer) {
    constexpr int size = 128;
    std::vector<unsigned char> pixels (size * size * 4, 255);
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const float px = 2.0f * (x + 0.5f) / size - 1.0f;
        const float py = (y + 0.5f) / size;
        float coverage = smoothstep (0.055f, 0.025f, std::abs (px)) *
                         smoothstep (-0.02f, 0.04f, py) *
                         (1.0f - smoothstep (0.96f, 1.02f, py));
        constexpr int needle_pairs = 16;
        for (int pair = 0; pair < needle_pairs; ++pair) {
          const float along = (pair + 0.7f) / (needle_pairs + 0.5f);
          const float span = 0.10f + 0.70f * (1.0f - along);
          for (float side : { -1.0f, 1.0f }) {
            const float cx = side * span * (0.48f + 0.08f * (pair % 3));
            const float cy = along + 0.012f * (pair % 2);
            const float turn = side * (0.11f + 0.025f * (pair % 4));
            const float cosine = std::cos (turn);
            const float sine = std::sin (turn);
            const float dx = px - cx;
            const float dy = py - cy;
            const float across =
              (cosine * dx + sine * dy) / (0.50f * span + 0.035f);
            const float along_needle = (-sine * dx + cosine * dy) / 0.052f;
            const float shape = std::pow (std::abs (across), 1.35f) +
                                std::pow (std::abs (along_needle), 1.55f);
            coverage = std::max (coverage, smoothstep (1.05f, 0.88f, shape));
          }
        }
        pixels[(y * size + x) * 4 + 3] = static_cast<unsigned char> (
          std::clamp (std::lround (255.0f * coverage), 0l, 255l));
      }

    render::TextureDesc description;
    description.width = size;
    description.height = size;
    description.format = render::TextureFormat::RGBA8;
    description.filter = render::TextureFilter::Mipmap;
    description.wrap = render::TextureWrap::Clamp;
    description.max_anisotropy = 4.0f;
    return renderer.create_texture (description, pixels.data ());
  }

  render::TexturePtr make_conifer_crown_texture (render::Renderer& renderer) {
    constexpr int size = 128;
    std::vector<unsigned char> pixels (size * size * 4, 255);
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const float px = 2.0f * (x + 0.5f) / size - 1.0f;
        const float py = (y + 0.5f) / size;
        float coverage = smoothstep (0.045f, 0.018f, std::abs (px)) *
                         (1.0f - smoothstep (0.98f, 1.02f, py));
        constexpr int whorls = 9;
        for (int whorl = 0; whorl < whorls; ++whorl) {
          const float share = static_cast<float> (whorl) / (whorls - 1);
          const float cy = 0.10f + 0.096f * whorl;
          const float span = 0.92f * (1.0f - 0.82f * share);
          const float across = px / span;
          const float along = (py - cy) / (0.060f - 0.018f * share);
          const float shape = std::pow (std::abs (across), 1.18f) +
                              std::pow (std::abs (along), 1.72f);
          coverage = std::max (coverage, smoothstep (1.08f, 0.84f, shape));
        }
        pixels[(y * size + x) * 4 + 3] = static_cast<unsigned char> (
          std::clamp (std::lround (255.0f * coverage), 0l, 255l));
      }

    render::TextureDesc description;
    description.width = size;
    description.height = size;
    description.format = render::TextureFormat::RGBA8;
    description.filter = render::TextureFilter::Mipmap;
    description.wrap = render::TextureWrap::Clamp;
    description.max_anisotropy = 4.0f;
    return renderer.create_texture (description, pixels.data ());
  }

  void foliage_triangle (render::DrawList& draw,
                         const FoliagePalette& palette,
                         const FoliageVertex& a,
                         const FoliageVertex& b,
                         const FoliageVertex& c) {
    foliage_vertex (draw, palette, a);
    foliage_vertex (draw, palette, b);
    foliage_vertex (draw, palette, c);
  }

  void foliage_quad (render::DrawList& draw,
                     const FoliagePalette& palette,
                     const FoliageVertex& a,
                     const FoliageVertex& b,
                     const FoliageVertex& c,
                     const FoliageVertex& d) {
    foliage_triangle (draw, palette, a, b, c);
    foliage_triangle (draw, palette, a, c, d);
  }
}
