#include <moppe/game/avatar.hh>
#include <moppe/game/car_render.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    Mat4 steering_turn (const CarPose& car, const CarModel& model) {
      const direction_t column =
        direction_of (model.steering_column - model.steering_hub);
      const radians_t turn { model.steering_ratio * car.steer };
      return Mat4::rotation (turn, direction_value (column));
    }

    // The hiker in a front seat: the driver holding the wheel with feet
    // at the pedals, the passenger with hands on their knees.
    AvatarSkeleton seated (const CarPose& car,
                           const CarModel& model,
                           const Mat4& frame,
                           bool driving) {
      // The hips above the seat, the hands short of the grips, and the
      // ankles above the pedals; the passenger's hands, on their knees,
      // aside, ahead of, and above the hips.
      const meters_t hip_rise = 0.09f * u::m;
      const meters_t grip_short = 0.07f * u::m;
      const meters_t ankle_rise = 0.085f * u::m;
      const meters_t hand_aside = 0.13f * u::m;
      const meters_t hand_ahead = 0.33f * u::m;
      const meters_t hand_rise = 0.10f * u::m;

      const direction_t up =
        unit_direction (normalized (frame.transform_vector (Vec3 (0, 1, 0))));
      const direction_t facing =
        unit_direction (normalized (frame.transform_vector (Vec3 (0, 0, 1))));
      const direction_t left = unit_direction (
        normalized (cross (direction_value (up), direction_value (facing))));
      const displacement_t seat =
        driving ? model.seat_driver : model.seat_passenger;
      // The figure's first arm and leg are those on its -x side, which
      // is the car's right.
      const displacement_t foot[2] = {
        driving ? model.pedal_right : model.footrest_right,
        driving ? model.pedal_left : model.footrest_left
      };
      const position_t pelvis =
        position (frame.transform_point (displacement_value (seat))) +
        quantity_cast<isq::position_vector> (along (up, hip_rise));

      HoldPose hold;
      hold.facing = direction_value (facing);
      hold.up = direction_value (up);
      hold.pelvis = position_value (pelvis);
      hold.lean = radians_value (model.lean);
      hold.gaze = direction_value (facing);
      hold.head_up = direction_value (up);
      hold.sole = normalized (direction_value (facing) +
                              direction_value (up) *
                                model.sole_tilt.numerical_value_in (one));
      hold.instep = normalized (direction_value (up) -
                                direction_value (facing) *
                                  model.sole_tilt.numerical_value_in (one));

      const Mat4 wheel =
        frame * Mat4::translation (displacement_value (model.steering_hub)) *
        steering_turn (car, model);
      const position_t grip[2] = {
        position (wheel.transform_point (
          displacement_value (model.grip_right - model.steering_hub))),
        position (wheel.transform_point (
          displacement_value (model.grip_left - model.steering_hub)))
      };
      for (int i = 0; i < 2; ++i) {
        const scale_factor_t side = (i == 0 ? -1.0f : 1.0f) * scale_factor[one];
        if (driving) {
          const position_t shoulder = position (held_shoulder (hold, i));
          const direction_t reach = direction_of (
            quantity_cast<isq::displacement> (grip[i] - shoulder));
          hold.wrist[i] =
            position_value (grip[i] - quantity_cast<isq::position_vector> (
                                        along (reach, grip_short)));
        } else {
          hold.wrist[i] = position_value (
            pelvis +
            quantity_cast<isq::position_vector> (
              along (left, side.numerical_value_in (one) * hand_aside)) +
            quantity_cast<isq::position_vector> (along (facing, hand_ahead)) +
            quantity_cast<isq::position_vector> (along (up, hand_rise)));
        }
        hold.elbow_pole[i] =
          direction_value (left) * side.numerical_value_in (one) -
          direction_value (up) * 0.6f - direction_value (facing) * 0.2f;
        hold.ankle[i] = position_value (
          position (frame.transform_point (displacement_value (foot[i]))) +
          quantity_cast<isq::position_vector> (along (up, ankle_rise)));
        hold.knee_pole[i] =
          direction_value (up) +
          direction_value (facing) * model.knee_ahead.numerical_value_in (one) +
          direction_value (left) *
            (side * model.knee_out).numerical_value_in (one);
      }
      return pose_holding (hold);
    }

    // The car's frame at its size.
    Mat4 car_frame (const CarPose& car) {
      return model_mesh::body_frame (car.position, car.forward, car.up) *
             Mat4::scaling (Vec3 (1, 1, 1) *
                            car.scale.numerical_value_in (one));
    }
  }

  void seat_car_passenger (render::DrawList& dl,
                           const CarPose& car,
                           const CarModel& model,
                           Outfit outfit) {
    const Mat4 frame = car_frame (car);
    figure::draw (dl, seated (car, model, frame, false), outfit);
  }

  void render_car (render::Renderer& r,
                   render::DrawList& dl,
                   const CarPose& car,
                   JeepRiders riders,
                   uint64_t motion_base,
                   const CarModel& model,
                   const CarMeshes& meshes) {
    const Mat4 frame = car_frame (car);
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
                   part.hinge ? frame * Mat4::translation (
                                          displacement_value (*part.hinge))
                              : frame);
    };
    draw_parts (meshes.body);
    draw_part (*meshes.steering_wheel,
               frame *
                 Mat4::translation (displacement_value (model.steering_hub)) *
                 steering_turn (car, model));

    const direction_t x_axis = unit_direction (Vec3 (1, 0, 0));
    const direction_t y_axis = unit_direction (Vec3 (0, 1, 0));
    const radians_t about = 3.14159265f * u::rad;
    for (int wheel = 0; wheel < 4; ++wheel) {
      const bool left_side = wheel % 2 == 0;
      Mat4 turn = Mat4::identity ();
      if (wheel < 2)
        turn = Mat4::rotation (-car.steer, direction_value (y_axis));
      // The model's wheel is a right-hand one; the left-hand ones are
      // turned about, which turns their roll about with them.
      if (left_side)
        turn = turn * Mat4::rotation (about, direction_value (y_axis));
      const radians_t roll = left_side ? -car.wheel_spin : car.wheel_spin;
      const displacement_t hub =
        model.wheels[wheel] -
        displacement (Vec3 (0, meters_value (car.wheel_drop[wheel]), 0));
      draw_part (wheel >= 2 && meshes.rear_wheel ? *meshes.rear_wheel
                                                 : *meshes.wheel,
                 frame * Mat4::translation (displacement_value (hub)) * turn *
                   Mat4::rotation (roll, direction_value (x_axis)));
    }
    draw_parts (meshes.glass);
  }
}
