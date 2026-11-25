#ifndef MOPPE_GAME_MODEL_MESH_HH
#define MOPPE_GAME_MODEL_MESH_HH

#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <cstdint>
#include <string_view>

namespace moppe::game::model_mesh {
  // A rigid assembly modelled in Blender and exported by
  // tools/figure/export_model.py into compressed Arrow bundles under
  // data/models/<model>/{vertices,triangles,paints[,film]}.arrows.
  struct Mesh {
    std::string_view model;
    std::uint32_t first_vertex = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t first_triangle = 0;
    std::uint32_t triangle_count = 0;
    bool has_film = false;
  };

  // Appends the mesh's triangles to `dl` under its current transform.
  // `reversed` turns each face inside out (winding swapped, normals negated),
  // for panes seen from both sides.
  void record (render::DrawList& dl, const Mesh& mesh, bool reversed = false);

  // The mesh recorded alone and retained by the renderer, to be replayed
  // with a model matrix. The machines and the animals bake their parts
  // once, the first time they are drawn.
  render::MeshPtr bake (render::Renderer& r, const Mesh& mesh);

  // Drops the single-model bundle cache kept across adjacent part bakes.
  void clear_cache ();

  // Model space for a machine standing at `position`: x to its left, y
  // up, z along `forward`, squared up from `up`.
  Mat4 body_frame (const position_t& position,
                   const direction_t& forward,
                   const direction_t& up);
}

#endif
