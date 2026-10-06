#ifndef MOPPE_GAME_FIGURE_HH
#define MOPPE_GAME_FIGURE_HH

#include <moppe/color.hh>
#include <moppe/game/avatar.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace moppe::game::figure {
  // The hiker, modelled and rigged in models/hiker.blend and exported to
  // figure_mesh.cc by tools/figure/export.py. Pairs are left then right,
  // matching AvatarSkeleton's indices.
  enum Bone : std::uint8_t {
    pelvis,
    spine,
    chest,
    head,
    thigh,
    shin = thigh + 2,
    foot = shin + 2,
    upper_arm = foot + 2,
    forearm = upper_arm + 2,
    hand = forearm + 2,
    bone_count = hand + 2
  };

  // A bone's frame: its origin, and its axes -- y along the bone, z toward
  // the reference the bone is rolled to, x = y cross z.
  struct Frame {
    std::array<float, 3> origin;
    std::array<float, 3> x, y, z;
  };

  // A vertex of the rest mesh with its smooth normal and up to four bones,
  // whose weights sum to one.
  struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::uint8_t bone[4];
    float weight[4];
  };

  struct Triangle {
    std::uint16_t vertex[3];
    std::uint8_t colour;
  };

  extern const std::span<const Frame, bone_count> rest;
  extern const std::span<const DisplayColor> colours;
  extern const std::span<const Vertex> vertices;
  extern const std::span<const Triangle> triangles;

  // Each bone's frame in the posed skeleton.
  std::array<Frame, bone_count> pose (const AvatarSkeleton& k);

  // The rest mesh's vertices and normals carried to the pose `k`, by
  // linear blend skinning; the normals take the blended rotation.
  void skin (const AvatarSkeleton& k,
             std::vector<Vec3>& points,
             std::vector<Vec3>& normals);

  // Skins the figure to `k` and draws it as smooth-shaded triangles.
  void draw (render::DrawList& dl, const AvatarSkeleton& k);
}

#endif
