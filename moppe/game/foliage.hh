#ifndef MOPPE_GAME_FOLIAGE_HH
#define MOPPE_GAME_FOLIAGE_HH

#include <moppe/game/foliage_kind.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>

#include <cstdint>

namespace moppe::render {
  class Renderer;
}

// What makes a handful of triangles read as a mass of leaves.
//
// A cheap crown volume fails in two ways that have nothing to do with how
// many triangles it has. It lights by facet, so a five-sided drum reads as
// folded paper; and it carries one flat colour, so a whole hillside reads as
// one painted green. Both are decided when the mesh is baked and cost
// nothing at all to draw.
//
// So every vertex of a plant carries the outward direction of the volume it
// belongs to rather than of the triangle it happens to sit in, and how much
// sky its part of the canopy sees. Species, ground, and plain individual
// variation choose the two ends of the colour ramp that exposure runs along.
// Bark is the same mechanism with a different ramp: dark where the stem meets
// the ground, lit where it rises clear.

namespace moppe::game {
  // The ground a crown answers to. A well watered closed canopy is deep and
  // saturated; a dry opening is olive and pale.
  struct FoliageGround {
    float moisture = 0.5f;
    float cover = 0.5f;
  };

  // The two ends of one plant surface's colour ramp: what the sky reaches and
  // what stays inside. Interpolating between them by exposure is the whole of
  // canopy self-shadowing at this scale.
  struct FoliagePalette {
    Vec3 sunlit;
    Vec3 shaded;
  };

  [[nodiscard]] FoliagePalette
  crown_palette (FoliageKind kind, FoliageGround ground, std::uint32_t seed);

  [[nodiscard]] FoliagePalette bark_palette (FoliageKind kind,
                                             std::uint32_t seed);

  // One shared white-and-coverage atlas for near leaf cards. Colour remains
  // an ecological property of the individual tree; the texture contributes
  // only a perforated leaf silhouette with filtered alpha at distance.
  [[nodiscard]] render::TexturePtr
  make_leaf_card_texture (render::Renderer& renderer);

  [[nodiscard]] render::TexturePtr
  make_conifer_card_texture (render::Renderer& renderer);

  [[nodiscard]] render::TexturePtr
  make_conifer_crown_texture (render::Renderer& renderer);

  // Distance does the rest. Once a crown is a pixel or two across, the scene
  // shader converges its albedo on the canopy tone the terrain is already
  // painting underneath, so a far tree joins the mass it belongs to instead
  // of speckling it. That belongs there and not here: done continuously
  // against distance it costs nothing and pops at no LOD boundary, whereas a
  // second palette baked into the far mesh would pop at exactly the one.

  // One vertex of a plant volume.
  struct FoliageVertex {
    Vec3 point;
    Vec3 outward;   // of the volume, not of the triangle
    float exposure; // 0 = deep interior or root, 1 = sunlit outer skin
    proportion_t bend;
    proportion_t flutter;
    float u = 0.0f;
    float v = 0.0f;
  };

  // Plants are recorded two-sided -- a leaf has no back -- so winding carries
  // no information and the three vertices go down in the order given.
  void foliage_triangle (render::DrawList& draw,
                         const FoliagePalette& palette,
                         const FoliageVertex& a,
                         const FoliageVertex& b,
                         const FoliageVertex& c);

  void foliage_quad (render::DrawList& draw,
                     const FoliagePalette& palette,
                     const FoliageVertex& a,
                     const FoliageVertex& b,
                     const FoliageVertex& c,
                     const FoliageVertex& d);
}

#endif
