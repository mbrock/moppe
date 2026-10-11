#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/jeep_model.hh>
#include <moppe/game/jeep_render.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    // The steering wheel turns this many times as far as the road wheels.
    constexpr float steering_ratio = 3.2f;

    struct JeepMeshes {
      render::MeshPtr body;
      render::MeshPtr cage;
      render::MeshPtr seats;
      render::MeshPtr column;
      render::MeshPtr steering_wheel;
      render::MeshPtr wheel;
    };

    render::MeshPtr bake_jeep_part (render::Renderer& r,
                                    const model_mesh::Mesh& mesh) {
      render::DrawList dl;
      model_mesh::record (dl, mesh);
      return r.create_mesh (dl);
    }

    const JeepMeshes& jeep_meshes (render::Renderer& r) {
      static const JeepMeshes meshes = [&r] {
        JeepMeshes m;
        m.body = bake_jeep_part (r, jeep_model::body);
        m.cage = bake_jeep_part (r, jeep_model::cage);
        m.seats = bake_jeep_part (r, jeep_model::seats);
        m.column = bake_jeep_part (r, jeep_model::column);
        m.steering_wheel = bake_jeep_part (r, jeep_model::steering_wheel);
        m.wheel = bake_jeep_part (r, jeep_model::wheel);
        return m;
      }();
      return meshes;
    }

    Mat4 jeep_frame (const JeepPose& jeep) {
      const Vec3 forward = normalized (jeep.forward);
      const Vec3 left = normalized (cross (jeep.up, forward));
      const Vec3 up = cross (forward, left);
      return Mat4::translation (jeep.position) *
             Mat4::basis (left, up, forward);
    }

    Mat4 steering_turn (const JeepPose& jeep) {
      const Vec3 column =
        normalized (jeep_model::steering_column - jeep_model::steering_hub);
      return Mat4::rotation (steering_ratio * jeep.steer_radians * u::rad,
                             column);
    }

    // The hiker in a seat: the driver holding the wheel with feet at the
    // pedals, the passenger with hands on their knees.
    AvatarSkeleton
    pose_seated (const JeepPose& jeep, const Mat4& frame, bool driving) {
      const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
      const Vec3 facing = normalized (frame.transform_vector (Vec3 (0, 0, 1)));
      const Vec3 left = normalized (cross (up, facing));
      const Vec3 seat =
        driving ? jeep_model::seat_driver : jeep_model::seat_passenger;
      // The figure's first arm and leg are those on its -x side, which
      // is the jeep's right.
      const Vec3 foot[2] = {
        driving ? jeep_model::pedal_right : jeep_model::footrest_right,
        driving ? jeep_model::pedal_left : jeep_model::footrest_left
      };

      HoldPose hold;
      hold.facing = facing;
      hold.up = up;
      hold.pelvis = frame.transform_point (seat) + up * 0.09f;
      // Sat back into the seat.
      hold.lean = -0.14f;
      hold.gaze = facing;
      hold.head_up = up;
      hold.sole = normalized (facing + up * 0.35f);
      hold.instep = normalized (up - facing * 0.35f);

      const Mat4 wheel = frame * Mat4::translation (jeep_model::steering_hub) *
                         steering_turn (jeep);
      const Vec3 grip[2] = { wheel.transform_point (jeep_model::grip_right -
                                                    jeep_model::steering_hub),
                             wheel.transform_point (jeep_model::grip_left -
                                                    jeep_model::steering_hub) };
      for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? -1.0f : 1.0f;
        if (driving) {
          const Vec3 shoulder = held_shoulder (hold, i);
          hold.wrist[i] = grip[i] - normalized (grip[i] - shoulder) * 0.07f;
        } else {
          hold.wrist[i] =
            hold.pelvis + left * (side * 0.13f) + facing * 0.33f + up * 0.10f;
        }
        hold.elbow_pole[i] = left * side - up * 0.6f - facing * 0.2f;
        hold.ankle[i] = frame.transform_point (foot[i]) + up * 0.085f;
        hold.knee_pole[i] = up + facing * 0.5f + left * (side * 0.2f);
      }
      return pose_holding (hold);
    }
  }

  void render_jeep (render::Renderer& r,
                    render::DrawList& dl,
                    const JeepPose& jeep,
                    JeepRiders riders,
                    uint64_t motion_base) {
    const JeepMeshes& meshes = jeep_meshes (r);
    const Mat4 frame = jeep_frame (jeep);
    if (riders.driver)
      figure::draw (dl, pose_seated (jeep, frame, true), riders.driver_outfit);
    if (riders.passenger)
      figure::draw (
        dl, pose_seated (jeep, frame, false), riders.passenger_outfit);

    uint64_t motion_part = motion_base;
    const auto draw_part = [&] (const render::Mesh& mesh,
                                const Mat4& transform) {
      r.draw_mesh (mesh, transform, ++motion_part);
    };
    draw_part (*meshes.body, frame);
    draw_part (*meshes.cage, frame);
    draw_part (*meshes.seats, frame);
    draw_part (*meshes.column, frame);
    draw_part (*meshes.steering_wheel,
               frame * Mat4::translation (jeep_model::steering_hub) *
                 steering_turn (jeep));

    const Vec3 x_axis (1, 0, 0), y_axis (0, 1, 0);
    const Vec3 axles[4] = { jeep_model::wheel_fl,
                            jeep_model::wheel_fr,
                            jeep_model::wheel_rl,
                            jeep_model::wheel_rr };
    for (int wheel = 0; wheel < 4; ++wheel) {
      const bool left_side = wheel % 2 == 0;
      Mat4 turn = Mat4::identity ();
      if (wheel < 2)
        turn = Mat4::rotation (-jeep.steer_radians * u::rad, y_axis);
      // The model's wheel is a right-hand one; the left-hand ones are
      // turned about, which turns their roll about with them.
      if (left_side)
        turn = turn * Mat4::rotation (3.14159265f * u::rad, y_axis);
      const float roll =
        left_side ? -jeep.wheel_spin_radians : jeep.wheel_spin_radians;
      draw_part (*meshes.wheel,
                 frame *
                   Mat4::translation (axles[wheel] -
                                      Vec3 (0, jeep.wheel_drop[wheel], 0)) *
                   turn * Mat4::rotation (roll * u::rad, x_axis));
    }
  }
}
