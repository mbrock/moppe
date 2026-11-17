#include <moppe/game/avatar.hh>
#include <moppe/game/car_render.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    Mat4 steering_turn (const CarPose& car, const CarModel& model) {
      const Vec3 column =
        normalized (model.steering_column - model.steering_hub);
      return Mat4::rotation (model.steering_ratio * car.steer_radians * u::rad,
                             column);
    }

    // The hiker in a front seat: the driver holding the wheel with feet
    // at the pedals, the passenger with hands on their knees.
    AvatarSkeleton seated (const CarPose& car,
                           const CarModel& model,
                           const Mat4& frame,
                           bool driving) {
      const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
      const Vec3 facing = normalized (frame.transform_vector (Vec3 (0, 0, 1)));
      const Vec3 left = normalized (cross (up, facing));
      const Vec3 seat = driving ? model.seat_driver : model.seat_passenger;
      // The figure's first arm and leg are those on its -x side, which
      // is the car's right.
      const Vec3 foot[2] = { driving ? model.pedal_right : model.footrest_right,
                             driving ? model.pedal_left : model.footrest_left };

      HoldPose hold;
      hold.facing = unit_direction (facing);
      hold.up = unit_direction (up);
      hold.pelvis = position (frame.transform_point (seat) + up * 0.09f);
      hold.lean = model.lean * u::rad;
      hold.gaze = facing;
      hold.head_up = up;
      hold.sole = unit_direction (normalized (facing + up * model.sole_tilt));
      hold.instep = unit_direction (normalized (up - facing * model.sole_tilt));

      const Mat4 wheel = frame * Mat4::translation (model.steering_hub) *
                         steering_turn (car, model);
      const Vec3 grip[2] = {
        wheel.transform_point (model.grip_right - model.steering_hub),
        wheel.transform_point (model.grip_left - model.steering_hub)
      };
      for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? -1.0f : 1.0f;
        if (driving) {
          const Vec3 shoulder = position_value (held_shoulder (hold, i));
          hold.wrist[i] =
            position (grip[i] - normalized (grip[i] - shoulder) * 0.07f);
        } else {
          position_value (hold.wrist[i]) = position_value (hold.pelvis) +
                                           left * (side * 0.13f) +
                                           facing * 0.33f + up * 0.10f;
        }
        hold.elbow_pole[i] = left * side - up * 0.6f - facing * 0.2f;
        hold.ankle[i] =
          position (frame.transform_point (foot[i]) + up * 0.085f);
        hold.knee_pole[i] =
          up + facing * model.knee_ahead + left * (side * model.knee_out);
      }
      return pose_holding (hold);
    }
  }

  void seat_car_passenger (render::DrawList& dl,
                           const CarPose& car,
                           const CarModel& model,
                           Outfit outfit) {
    const Mat4 frame =
      model_mesh::body_frame (car.position, car.forward, car.up) *
      Mat4::scaling (Vec3 (1, 1, 1) * car.scale);
    figure::draw (dl, seated (car, model, frame, false), outfit);
  }

  void render_car (render::Renderer& r,
                   render::DrawList& dl,
                   const CarPose& car,
                   JeepRiders riders,
                   uint64_t motion_base,
                   const CarModel& model,
                   const CarMeshes& meshes) {
    const Mat4 frame =
      model_mesh::body_frame (car.position, car.forward, car.up) *
      Mat4::scaling (Vec3 (1, 1, 1) * car.scale);
    if (riders.driver)
      figure::draw (dl, seated (car, model, frame, true), riders.driver_outfit);
    if (riders.passenger)
      figure::draw (
        dl, seated (car, model, frame, false), riders.passenger_outfit);

    uint64_t motion_part = motion_base;
    const auto draw_part = [&] (const render::Mesh& mesh,
                                const Mat4& transform) {
      r.draw_mesh (mesh, transform, ++motion_part);
    };
    const auto draw_parts = [&] (const std::vector<CarMeshes::Part>& parts) {
      for (const CarMeshes::Part& part : parts)
        draw_part (*part.mesh,
                   part.hinge ? frame * Mat4::translation (*part.hinge)
                              : frame);
    };
    draw_parts (meshes.body);
    draw_part (*meshes.steering_wheel,
               frame * Mat4::translation (model.steering_hub) *
                 steering_turn (car, model));

    const Vec3 x_axis (1, 0, 0), y_axis (0, 1, 0);
    for (int wheel = 0; wheel < 4; ++wheel) {
      const bool left_side = wheel % 2 == 0;
      Mat4 turn = Mat4::identity ();
      if (wheel < 2)
        turn = Mat4::rotation (-car.steer_radians * u::rad, y_axis);
      // The model's wheel is a right-hand one; the left-hand ones are
      // turned about, which turns their roll about with them.
      if (left_side)
        turn = turn * Mat4::rotation (3.14159265f * u::rad, y_axis);
      const float roll =
        left_side ? -car.wheel_spin_radians : car.wheel_spin_radians;
      draw_part (wheel >= 2 && meshes.rear_wheel ? *meshes.rear_wheel
                                                 : *meshes.wheel,
                 frame *
                   Mat4::translation (model.wheels[wheel] -
                                      Vec3 (0, car.wheel_drop[wheel], 0)) *
                   turn * Mat4::rotation (roll * u::rad, x_axis));
    }
    draw_parts (meshes.glass);
  }
}
