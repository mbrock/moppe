#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>

#include <algorithm>

namespace moppe::game::model_mesh {
  void record (render::DrawList& dl, const Mesh& mesh) {
    // What lets light through (glass, a lamp's lens is lit) is drawn
    // blended and leaves the depth to what stands behind it: its colour's
    // alpha is how much of its own tint it puts over that.
    const auto see_through = [] (const Paint& paint) {
      return paint.transmission > 0.5f * proportion[one] && !paint.unlit;
    };
    const render::DrawState before = dl.state ();
    const bool has_glass = std::ranges::any_of (mesh.paints, see_through);
    for (const bool clear : { false, true }) {
      if (clear && !has_glass)
        break;
      render::DrawState state = before;
      state.glass = clear;
      state.blend = clear || before.blend;
      state.depth_write = !clear && before.depth_write;
      dl.state (state);
      dl.begin (render::Prim::Triangles);
      for (const Triangle& t : mesh.triangles) {
        const Paint& paint = mesh.paints[t.paint];
        if (has_glass && see_through (paint) != clear)
          continue;
        dl.lit (!paint.unlit);
        const proportion_t opaque = 1.0f * proportion[one];
        dl.color (paint.colour,
                  clear ? opaque - 0.7f * paint.transmission : opaque);
        dl.material (paint.metallic,
                     paint.roughness,
                     paint.coat,
                     paint.sheen,
                     paint.coat_roughness);
        // The exported vertex table is raw storage: each vertex becomes a
        // model-space offset and a unit normal here. Its wear stays the
        // plain float, whose negative means no film.
        for (const std::uint16_t i : t.vertex) {
          const Vertex& v = mesh.vertices[i];
          dl.normal (
            unit_direction (Vec3 (v.normal[0], v.normal[1], v.normal[2])));
          dl.uv (v.film[0], v.film[1]);
          dl.worn (v.wear);
          dl.vertex (
            displacement (Vec3 (v.position[0], v.position[1], v.position[2])));
        }
      }
      dl.end ();
    }
    dl.state (before);
    dl.lit (true);
    dl.plain ();
    dl.uv (0.0f, 0.0f);
    dl.worn (-1.0f);
  }

  render::MeshPtr bake (render::Renderer& r, const Mesh& mesh) {
    render::DrawList dl;
    record (dl, mesh);
    return r.create_mesh (dl);
  }

  Mat4 body_frame (const position_t& position,
                   const direction_t& forward,
                   const direction_t& up) {
    // Normalized again: a pose's forward is only nearly unit.
    const direction_t ahead =
      unit_direction (normalized (direction_value (forward)));
    const direction_t left = unit_direction (
      normalized (cross (direction_value (up), direction_value (ahead))));
    return Mat4::translation (position_value (position)) *
           Mat4::basis (direction_value (left),
                        cross (direction_value (ahead), direction_value (left)),
                        direction_value (ahead));
  }
}
