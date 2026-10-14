#include <moppe/game/helicopter_model.hh>
#include <moppe/game/helicopter_render.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>

namespace moppe::game {
  namespace {
    struct HelicopterMeshes {
      render::MeshPtr body;
      render::MeshPtr main_rotor;
      render::MeshPtr tail_rotor;
    };

    render::MeshPtr bake_helicopter_part (render::Renderer& r,
                                          const model_mesh::Mesh& mesh) {
      render::DrawList dl;
      model_mesh::record (dl, mesh);
      return r.create_mesh (dl);
    }

    const HelicopterMeshes& helicopter_meshes (render::Renderer& r) {
      static const HelicopterMeshes meshes = [&r] {
        HelicopterMeshes m;
        m.body = bake_helicopter_part (r, helicopter_model::body);
        m.main_rotor = bake_helicopter_part (r, helicopter_model::main_rotor);
        m.tail_rotor = bake_helicopter_part (r, helicopter_model::tail_rotor);
        return m;
      }();
      return meshes;
    }
  }

  void render_helicopter (render::Renderer& r,
                          const HelicopterPose& helicopter,
                          uint64_t motion_base) {
    const HelicopterMeshes& meshes = helicopter_meshes (r);
    const Vec3 forward = normalized (helicopter.forward);
    const Vec3 left = normalized (cross (helicopter.up, forward));
    const Vec3 up = cross (forward, left);
    const Mat4 frame =
      Mat4::translation (helicopter.position) * Mat4::basis (left, up, forward);
    r.draw_mesh (*meshes.body, frame, motion_base + 1);
    r.draw_mesh (
      *meshes.main_rotor,
      frame * Mat4::translation (helicopter_model::rotor_hub) *
        Mat4::rotation (helicopter.rotor_radians * u::rad, Vec3 (0, 1, 0)),
      motion_base + 2);
    r.draw_mesh (
      *meshes.tail_rotor,
      frame * Mat4::translation (helicopter_model::tail_hub) *
        Mat4::rotation (helicopter.tail_radians * u::rad, Vec3 (1, 0, 0)),
      motion_base + 3);
  }
}
