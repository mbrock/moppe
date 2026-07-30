#include <moppe/game/foliage.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>

namespace moppe::game {
  namespace {
    using namespace mp_units;

    // Display-space greens, decoded to linear by the scene shader. The two
    // species differ in more than hue: a conifer holds its needles in deep
    // shaded columns, a broadleaf spreads a thinner brighter sheet.
    constexpr Vec3 broadleaf_sun (0.168f, 0.372f, 0.078f);
    constexpr Vec3 broadleaf_shade (0.034f, 0.122f, 0.052f);
    constexpr Vec3 conifer_sun (0.104f, 0.286f, 0.086f);
    constexpr Vec3 conifer_shade (0.022f, 0.090f, 0.058f);

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
                         ? Vec3 (0.225f, 0.116f, 0.062f)
                         : Vec3 (0.198f, 0.146f, 0.104f);
    const Vec3 lit = linear_vector_interpolate (
      fresh, Vec3 (0.168f, 0.152f, 0.136f), hash_lane (seed, 11));
    // Bark in shade is not black. It is lit by the ground and by whatever
    // gets through the crown, and treating it as an unlit silhouette is what
    // makes a stand look like fence posts driven into a lawn.
    return { lit, lit * 0.52f };
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
