#ifndef MOPPE_GAME_MODEL_MESH_HH
#define MOPPE_GAME_MODEL_MESH_HH

#include <moppe/color.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>

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
    float metallic = 0.0f;
    float roughness = 0.8f;
    float coat = 0.0f;
    float coat_roughness = 0.03f;
    float transmission = 0.0f;
    float ior = 1.5f;
    float sheen = 0.0f;
    float emission = 0.0f;
  };

  struct Mesh {
    std::span<const Vertex> vertices;
    std::span<const Triangle> triangles;
    std::span<const Paint> paints;
  };

  // Appends the mesh's triangles to `dl` under its current transform.
  void record (render::DrawList& dl, const Mesh& mesh);
}

#endif
