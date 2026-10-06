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
