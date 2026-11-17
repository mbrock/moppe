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

    const HelicopterMeshes& helicopter_meshes (render::Renderer& r) {
      static const HelicopterMeshes meshes = [&r] {
        HelicopterMeshes m;
        m.body = model_mesh::bake (r, helicopter_model::body);
        m.main_rotor = model_mesh::bake (r, helicopter_model::main_rotor);
        m.tail_rotor = model_mesh::bake (r, helicopter_model::tail_rotor);
        return m;
      }();
      return meshes;
    }
  }

  void render_helicopter (render::Renderer& r,
                          const HelicopterPose& helicopter,
                          uint64_t motion_base) {
    const HelicopterMeshes& meshes = helicopter_meshes (r);
    const Mat4 frame = model_mesh::body_frame (
      helicopter.position, helicopter.forward, helicopter.up);
    // The mast is the frame's up, the tail rotor's axle its left.
    const direction_t mast = unit_direction (Vec3 (0, 1, 0));
    const direction_t axle = unit_direction (Vec3 (1, 0, 0));
    const displacement_t rotor_hub = displacement (helicopter_model::rotor_hub);
    const displacement_t tail_hub = displacement (helicopter_model::tail_hub);
    r.draw_mesh (*meshes.body, frame, motion_base + 1);
    r.draw_mesh (*meshes.main_rotor,
                 frame * Mat4::translation (displacement_value (rotor_hub)) *
                   Mat4::rotation (helicopter.rotor, direction_value (mast)),
                 motion_base + 2);
    r.draw_mesh (*meshes.tail_rotor,
                 frame * Mat4::translation (displacement_value (tail_hub)) *
                   Mat4::rotation (helicopter.tail, direction_value (axle)),
                 motion_base + 3);
  }
}
