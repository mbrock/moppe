#ifndef MOPPE_GAME_MODEL_MESH_HH
#define MOPPE_GAME_MODEL_MESH_HH

#include <moppe/color.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <array>
#include <cstdint>
#include <span>

namespace moppe::game::model_mesh {
  // A rigid mesh modelled in Blender and exported by
  // tools/figure/export_model.py: vertices with their shading normals,
  // and triangles that each name a paint.
  struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    // On a weathered film (the boat's plywood): the film's own flattened
    // coordinates in metres, along the boat first, and how worn it is
    // there, 0..1; negative where there is no such film.
    std::array<float, 2> film = {};
    float wear = -1.0f;
  };

  struct Triangle {
    std::uint16_t vertex[3];
    std::uint8_t paint;
  };

  struct Paint {
    DisplayColor colour;
    // Drawn without lighting, like a lamp's lens.
    bool unlit;
    // The rest of the material's Principled BSDF, for the renderer to
    // shade by as it learns to: how metallic and how rough, a clear coat
    // over it (car paint) and its roughness, how much light passes
    // through (glass, lenses) and its index of refraction, the sheen of
    // cloth, and how strongly it glows in its own colour.
    // The exported tables give each as a bare number, which a proportion
    // and the refractive index take as they are.
    proportion_t metallic = 0.0f * proportion[one];
    proportion_t roughness = 0.8f * proportion[one];
    proportion_t coat = 0.0f * proportion[one];
    proportion_t coat_roughness = 0.03f * proportion[one];
    proportion_t transmission = 0.0f * proportion[one];
    quantity<isq::refractive_index[one], float> ior =
      1.5f * isq::refractive_index[one];
    proportion_t sheen = 0.0f * proportion[one];
    // A strength that may pass one (a lamp's 4): a scale_factor_t, once
    // the exporter writes it as one, since that kind takes no bare float.
    float emission = 0.0f;
  };

  struct Mesh {
    std::span<const Vertex> vertices;
    std::span<const Triangle> triangles;
    std::span<const Paint> paints;
  };

  // Appends the mesh's triangles to `dl` under its current transform.
  void record (render::DrawList& dl, const Mesh& mesh);

  // The mesh recorded alone and retained by the renderer, to be replayed
  // with a model matrix. The machines and the animals bake their parts
  // once, the first time they are drawn.
  render::MeshPtr bake (render::Renderer& r, const Mesh& mesh);

  // Model space for a machine standing at `position`: x to its left, y
  // up, z along `forward`, squared up from `up`.
  Mat4 body_frame (const position_t& position,
                   const direction_t& forward,
                   const direction_t& up);
  // The same for callers still holding plain vectors (BusPose).
  Mat4 body_frame (const Vec3& position, const Vec3& forward, const Vec3& up);
}

#endif
