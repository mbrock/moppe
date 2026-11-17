#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/tractor_model.hh>
#include <moppe/game/tractor_render.hh>
#include <moppe/game/trailer_model.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    // The engine is a unity build, and the jeep's drawing next door has
    // helpers of the same names.
    namespace tractor_drawing {
      // The steering wheel turns this many times as far as the road wheels.
      constexpr scale_factor_t steering_ratio = 4.0f * scale_factor[one];

      struct TractorMeshes {
        render::MeshPtr body;
        render::MeshPtr hitch;
        render::MeshPtr column;
        render::MeshPtr steering_wheel;
        render::MeshPtr front_wheel;
        render::MeshPtr rear_wheel;
        render::MeshPtr trailer;
        render::MeshPtr trailer_wheel;
      };

      const TractorMeshes& tractor_meshes (render::Renderer& r) {
        static const TractorMeshes meshes = [&r] {
          TractorMeshes m;
          m.body = model_mesh::bake (r, tractor_model::body);
          m.hitch = model_mesh::bake (r, tractor_model::hitch);
          m.column = model_mesh::bake (r, tractor_model::column);
          m.steering_wheel =
            model_mesh::bake (r, tractor_model::steering_wheel);
          m.front_wheel = model_mesh::bake (r, tractor_model::front_wheel);
          m.rear_wheel = model_mesh::bake (r, tractor_model::rear_wheel);
          m.trailer = model_mesh::bake (r, trailer_model::body);
          m.trailer_wheel = model_mesh::bake (r, trailer_model::wheel);
          return m;
        }();
        return meshes;
      }

      Mat4 steering_turn (const TractorPose& tractor) {
        const direction_t column =
          direction_of (displacement (tractor_model::steering_column) -
                        displacement (tractor_model::steering_hub));
        const radians_t turn { steering_ratio * tractor.steer };
        return Mat4::rotation (turn, direction_value (column));
      }

      // The hiker on the pan seat, holding the wheel, feet on the plates.
      AvatarSkeleton pose_driver (const TractorPose& tractor,
                                  const Mat4& frame) {
        // The hips above the seat, the hands short of the grips, and the
        // ankles above the plates.
        const meters_t hip_rise = 0.09f * u::m;
        const meters_t grip_short = 0.07f * u::m;
        const meters_t ankle_rise = 0.085f * u::m;
        // Sat up, leaning a little toward the wheel; the soles tipped up
        // to the plates (rise over run), and the knees ahead and out, as
        // shares of up.
        const radians_t lean = 0.05f * u::rad;
        const grade_t sole_tilt = 0.15f * grade[one];
        const scale_factor_t knee_ahead = 0.6f * scale_factor[one];
        const scale_factor_t knee_out = 0.35f * scale_factor[one];

        const direction_t up =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 1, 0))));
        const direction_t facing =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 0, 1))));
        const direction_t left = unit_direction (
          normalized (cross (direction_value (up), direction_value (facing))));
        // The figure's first arm and leg are those on its -x side, which
        // is the tractor's right.
        const displacement_t foot[2] = {
          displacement (tractor_model::pedal_right),
          displacement (tractor_model::pedal_left)
        };
        const displacement_t hub = displacement (tractor_model::steering_hub);

        HoldPose hold;
        hold.facing = direction_value (facing);
        hold.up = direction_value (up);
        hold.pelvis = position_value (
          position (frame.transform_point (tractor_model::seat_driver)) +
          quantity_cast<isq::position_vector> (along (up, hip_rise)));
        hold.lean = radians_value (lean);
        hold.gaze = direction_value (facing);
        hold.head_up = direction_value (up);
        hold.sole = normalized (direction_value (up) *
                                  sole_tilt.numerical_value_in (one) +
                                direction_value (facing));
        hold.instep = normalized (direction_value (up) -
                                  direction_value (facing) *
                                    sole_tilt.numerical_value_in (one));

        const Mat4 wheel = frame *
                           Mat4::translation (displacement_value (hub)) *
                           steering_turn (tractor);
        const position_t grip[2] = {
          position (wheel.transform_point (displacement_value (
            displacement (tractor_model::grip_right) - hub))),
          position (wheel.transform_point (
            displacement_value (displacement (tractor_model::grip_left) - hub)))
        };
        for (int i = 0; i < 2; ++i) {
          const scale_factor_t side =
            (i == 0 ? -1.0f : 1.0f) * scale_factor[one];
          const position_t shoulder = position (held_shoulder (hold, i));
          const direction_t reach = direction_of (
            quantity_cast<isq::displacement> (grip[i] - shoulder));
          hold.wrist[i] =
            position_value (grip[i] - quantity_cast<isq::position_vector> (
                                        along (reach, grip_short)));
          hold.elbow_pole[i] =
            direction_value (left) * side.numerical_value_in (one) -
            direction_value (up) * 0.6f - direction_value (facing) * 0.2f;
          hold.ankle[i] = position_value (
            position (frame.transform_point (displacement_value (foot[i]))) +
            quantity_cast<isq::position_vector> (along (up, ankle_rise)));
          hold.knee_pole[i] =
            direction_value (up) +
            direction_value (facing) * knee_ahead.numerical_value_in (one) +
            direction_value (left) * (side * knee_out).numerical_value_in (one);
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
    const Mat4 frame =
      model_mesh::body_frame (tractor.position, tractor.forward, tractor.up);
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

    const direction_t x_axis = unit_direction (Vec3 (1, 0, 0));
    const direction_t y_axis = unit_direction (Vec3 (0, 1, 0));
    const radians_t about = 3.14159265f * u::rad;
    const displacement_t axles[4] = { displacement (tractor_model::wheel_fl),
                                      displacement (tractor_model::wheel_fr),
                                      displacement (tractor_model::wheel_rl),
                                      displacement (tractor_model::wheel_rr) };
    for (int wheel = 0; wheel < 4; ++wheel) {
      const bool front = wheel < 2;
      const bool left_side = wheel % 2 == 0;
      Mat4 turn = Mat4::identity ();
      if (front)
        turn = Mat4::rotation (-tractor.steer, direction_value (y_axis));
      // The model's wheels are right-hand ones; the left-hand ones are
      // turned about, which turns their roll about with them.
      if (left_side)
        turn = turn * Mat4::rotation (about, direction_value (y_axis));
      const radians_t spin = front ? tractor.front_spin : tractor.rear_spin;
      const radians_t roll = left_side ? -spin : spin;
      const displacement_t hub =
        axles[wheel] -
        displacement (Vec3 (0, meters_value (tractor.wheel_drop[wheel]), 0));
      draw_part (front ? *meshes.front_wheel : *meshes.rear_wheel,
                 frame * Mat4::translation (displacement_value (hub)) * turn *
                   Mat4::rotation (roll, direction_value (x_axis)));
    }

    // The trailer behind, its wheels on their springs, the left-hand one
    // turned about as the tractor's are.
    const Mat4 towed = model_mesh::body_frame (
      tractor.trailer_position, tractor.trailer_forward, tractor.trailer_up);
    draw_part (*meshes.trailer, towed);
    const displacement_t trailer_axles[2] = {
      displacement (trailer_model::wheel_l),
      displacement (trailer_model::wheel_r)
    };
    for (int wheel = 0; wheel < 2; ++wheel) {
      const bool left_side = wheel == 0;
      const radians_t spin = tractor.trailer_spin;
      const Mat4 turn = left_side
                          ? Mat4::rotation (about, direction_value (y_axis))
                          : Mat4::identity ();
      const displacement_t hub =
        trailer_axles[wheel] -
        displacement (
          Vec3 (0, meters_value (tractor.trailer_wheel_drop[wheel]), 0));
      draw_part (
        *meshes.trailer_wheel,
        towed * Mat4::translation (displacement_value (hub)) * turn *
          Mat4::rotation (left_side ? -spin : spin, direction_value (x_axis)));
    }
  }
}
