#include <moppe/game/model_mesh.hh>

namespace moppe::game::model_mesh {
  void record (render::DrawList& dl, const Mesh& mesh) {
    // What lets light through (glass, a lamp's lens is lit) is drawn
    // blended and leaves the depth to what stands behind it: its colour's
    // alpha is how much of its own tint it puts over that.
    const auto see_through = [] (const Paint& paint) {
      return paint.transmission > 0.5f && !paint.unlit;
    };
    const render::DrawState before = dl.state ();
    bool clear = false;
    dl.begin (render::Prim::Triangles);
    for (const Triangle& t : mesh.triangles) {
      const Paint& paint = mesh.paints[t.paint];
      if (see_through (paint) != clear) {
        dl.end ();
        clear = see_through (paint);
        render::DrawState state = before;
        state.blend = clear;
        state.depth_write = !clear && before.depth_write;
        dl.state (state);
        dl.begin (render::Prim::Triangles);
      }
      dl.lit (!paint.unlit);
      dl.color (paint.colour, clear ? 1.0f - 0.7f * paint.transmission : 1.0f);
      dl.material (paint.metallic, paint.roughness, paint.coat, paint.sheen);
      for (const std::uint16_t i : t.vertex) {
        const Vertex& v = mesh.vertices[i];
        dl.normal (Vec3 (v.normal[0], v.normal[1], v.normal[2]));
        dl.uv (v.film[0], v.film[1]);
        dl.worn (v.wear);
        dl.vertex (Vec3 (v.position[0], v.position[1], v.position[2]));
      }
    }
    dl.end ();
    dl.state (before);
    dl.lit (true);
    dl.plain ();
    dl.uv (0.0f, 0.0f);
    dl.worn (-1.0f);
  }
}
