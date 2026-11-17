#include <moppe/game/glider_render.hh>

#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/glider_model.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    struct GliderMeshes {
      render::MeshPtr glider;
      render::MeshPtr tether;
    };

    const GliderMeshes& glider_meshes (render::Renderer& r) {
      static const GliderMeshes meshes {
        model_mesh::bake (r, glider_model::glider),
        model_mesh::bake (r, glider_model::tether)
      };
      return meshes;
    }

    Mat4 glider_frame (const GliderPose& glider) {
      const Vec3 fwd = normalized (glider.heading);
      const Vec3 right = normalized (cross (Vec3 (0, 1, 0), fwd));
      const Vec3 up = cross (fwd, right);
      return Mat4::translation (glider.position) *
             Mat4::basis (right, up, fwd) *
             Mat4::rotation (-glider.bank_radians * u::rad, Vec3 (0, 0, 1));
    }

    // The pilot lies prone in the harness, belly to the ground and head
    // toward the nose, chest lifted to look ahead, hands on the basebar
    // with the elbows out, legs trailing with the toes pointed; the body
    // sways a little on its strap.
    AvatarSkeleton pose_pilot (const Mat4& frame, float time) {
      const Vec3 forward = normalized (frame.transform_vector (Vec3 (0, 0, 1)));
      const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
      const Vec3 right = normalized (cross (up, forward));
      const float sway = std::sin (time * 3.1f) * 0.035f;

      HoldPose hold;
      hold.facing = unit_direction (-up);
      hold.up = unit_direction (forward);
      hold.pelvis = position (frame.transform_point (glider_model::harness) -
                              up * 0.12f - forward * 0.08f + right * sway);
      hold.lean = (-0.2f) * u::rad;
      hold.gaze = normalized (forward - up * 0.15f);
      hold.head_up = up;
      hold.sole = unit_direction (-forward);
      hold.instep = unit_direction (-up);
      const Vec3 grip[2] = { frame.transform_point (glider_model::grip_left),
                             frame.transform_point (glider_model::grip_right) };
      for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? -1.0f : 1.0f;
        const Vec3 shoulder = position_value (held_shoulder (hold, i));
        hold.wrist[i] =
          position (grip[i] - normalized (grip[i] - shoulder) * 0.07f);
        hold.elbow_pole[i] = right * (side * 0.8f) - up * 0.5f;
        position_value (hold.ankle[i]) = position_value (hold.pelvis) -
                                         forward * 0.84f + up * 0.03f +
                                         right * (side * 0.1f);
        hold.knee_pole[i] = -up;
      }
      return pose_holding (hold);
    }
  }

  void render_glider (render::Renderer& r,
                      render::DrawList& dl,
                      const GliderPose& glider,
                      float time,
                      uint64_t motion_base,
                      Outfit outfit) {
    const GliderMeshes& meshes = glider_meshes (r);
    const Mat4 frame = glider_frame (glider);
    r.draw_mesh (*meshes.glider, frame, motion_base + 1);
    if (glider.bike_attached)
      r.draw_mesh (*meshes.tether, frame, motion_base + 2);
    figure::draw (dl, pose_pilot (frame, time), outfit);
  }
}
