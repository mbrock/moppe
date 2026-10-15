#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/tractor_model.hh>
#include <moppe/game/tractor_render.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    // The engine is a unity build, and the jeep's drawing next door has
    // helpers of the same names.
    namespace tractor_drawing {
      // The steering wheel turns this many times as far as the road wheels.
      constexpr float steering_ratio = 4.0f;

      struct TractorMeshes {
        render::MeshPtr body;
        render::MeshPtr hitch;
        render::MeshPtr column;
        render::MeshPtr steering_wheel;
        render::MeshPtr front_wheel;
        render::MeshPtr rear_wheel;
      };

      render::MeshPtr bake_tractor_part (render::Renderer& r,
                                         const model_mesh::Mesh& mesh) {
        render::DrawList dl;
        model_mesh::record (dl, mesh);
        return r.create_mesh (dl);
      }

      const TractorMeshes& tractor_meshes (render::Renderer& r) {
        static const TractorMeshes meshes = [&r] {
          TractorMeshes m;
          m.body = bake_tractor_part (r, tractor_model::body);
          m.hitch = bake_tractor_part (r, tractor_model::hitch);
          m.column = bake_tractor_part (r, tractor_model::column);
          m.steering_wheel =
            bake_tractor_part (r, tractor_model::steering_wheel);
          m.front_wheel = bake_tractor_part (r, tractor_model::front_wheel);
          m.rear_wheel = bake_tractor_part (r, tractor_model::rear_wheel);
          return m;
        }();
        return meshes;
      }

      Mat4 tractor_frame (const TractorPose& tractor) {
        const Vec3 forward = normalized (tractor.forward);
        const Vec3 left = normalized (cross (tractor.up, forward));
        const Vec3 up = cross (forward, left);
        return Mat4::translation (tractor.position) *
               Mat4::basis (left, up, forward);
      }

      Mat4 steering_turn (const TractorPose& tractor) {
        const Vec3 column = normalized (tractor_model::steering_column -
                                        tractor_model::steering_hub);
        return Mat4::rotation (steering_ratio * tractor.steer_radians * u::rad,
                               column);
      }

      // The hiker on the pan seat, holding the wheel, feet on the plates.
      AvatarSkeleton pose_driver (const TractorPose& tractor,
                                  const Mat4& frame) {
        const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
        const Vec3 facing =
          normalized (frame.transform_vector (Vec3 (0, 0, 1)));
        const Vec3 left = normalized (cross (up, facing));
        // The figure's first arm and leg are those on its -x side, which
        // is the tractor's right.
        const Vec3 foot[2] = { tractor_model::pedal_right,
                               tractor_model::pedal_left };

        HoldPose hold;
        hold.facing = facing;
        hold.up = up;
        hold.pelvis =
          frame.transform_point (tractor_model::seat_driver) + up * 0.09f;
        // Sat up, leaning a little toward the wheel.
        hold.lean = 0.05f;
        hold.gaze = facing;
        hold.head_up = up;
        hold.sole = normalized (up * 0.15f + facing);
        hold.instep = normalized (up - facing * 0.15f);

        const Mat4 wheel = frame *
                           Mat4::translation (tractor_model::steering_hub) *
                           steering_turn (tractor);
        const Vec3 grip[2] = {
          wheel.transform_point (tractor_model::grip_right -
                                 tractor_model::steering_hub),
          wheel.transform_point (tractor_model::grip_left -
                                 tractor_model::steering_hub)
        };
        for (int i = 0; i < 2; ++i) {
          const float side = i == 0 ? -1.0f : 1.0f;
          const Vec3 shoulder = held_shoulder (hold, i);
          hold.wrist[i] = grip[i] - normalized (grip[i] - shoulder) * 0.07f;
          hold.elbow_pole[i] = left * side - up * 0.6f - facing * 0.2f;
          hold.ankle[i] = frame.transform_point (foot[i]) + up * 0.085f;
          hold.knee_pole[i] = up + facing * 0.6f + left * (side * 0.35f);
        }
        return pose_holding (hold);
      }
    }
  }
  using namespace tractor_drawing;

  void render_tractor (render::Renderer& r,
                       render::DrawList& dl,
                       const TractorPose& tractor,
                       std::optional<Outfit> driver,
                       uint64_t motion_base) {
    const TractorMeshes& meshes = tractor_meshes (r);
    const Mat4 frame = tractor_frame (tractor);
    if (driver)
      figure::draw (dl, pose_driver (tractor, frame), *driver);

    uint64_t motion_part = motion_base;
    const auto draw_part = [&] (const render::Mesh& mesh,
                                const Mat4& transform) {
      r.draw_mesh (mesh, transform, ++motion_part);
    };
    draw_part (*meshes.body, frame);
    draw_part (*meshes.hitch, frame);
    draw_part (*meshes.column, frame);
    draw_part (*meshes.steering_wheel,
               frame * Mat4::translation (tractor_model::steering_hub) *
                 steering_turn (tractor));

    const Vec3 x_axis (1, 0, 0), y_axis (0, 1, 0);
    const Vec3 axles[4] = { tractor_model::wheel_fl,
                            tractor_model::wheel_fr,
                            tractor_model::wheel_rl,
                            tractor_model::wheel_rr };
    for (int wheel = 0; wheel < 4; ++wheel) {
      const bool front = wheel < 2;
      const bool left_side = wheel % 2 == 0;
      Mat4 turn = Mat4::identity ();
      if (front)
        turn = Mat4::rotation (-tractor.steer_radians * u::rad, y_axis);
      // The model's wheels are right-hand ones; the left-hand ones are
      // turned about, which turns their roll about with them.
      if (left_side)
        turn = turn * Mat4::rotation (3.14159265f * u::rad, y_axis);
      const float spin =
        front ? tractor.front_spin_radians : tractor.rear_spin_radians;
      const float roll = left_side ? -spin : spin;
      draw_part (front ? *meshes.front_wheel : *meshes.rear_wheel,
                 frame *
                   Mat4::translation (axles[wheel] -
                                      Vec3 (0, tractor.wheel_drop[wheel], 0)) *
                   turn * Mat4::rotation (roll * u::rad, x_axis));
    }
  }
}
