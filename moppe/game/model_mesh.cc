#include <moppe/game/model_mesh.hh>

namespace moppe::game::model_mesh {
  void record (render::DrawList& dl, const Mesh& mesh) {
    dl.begin (render::Prim::Triangles);
    for (const Triangle& t : mesh.triangles) {
      const Paint& paint = mesh.paints[t.paint];
      dl.lit (!paint.unlit);
      dl.color (paint.colour);
      for (const std::uint16_t i : t.vertex) {
        const Vertex& v = mesh.vertices[i];
        dl.normal (Vec3 (v.normal[0], v.normal[1], v.normal[2]));
        dl.vertex (Vec3 (v.position[0], v.position[1], v.position[2]));
      }
    }
    dl.end ();
    dl.lit (true);
  }
}
