#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/mountain_bike_model.hh>
#include <moppe/game/mountain_bike_render.hh>
#include <moppe/gfx/mat4.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    // The engine is a unity build, and the other vehicles' drawing has
    // helpers of the same names.
    namespace mountain_bike_drawing {
      namespace model = mountain_bike_model;

      // How far outboard of the crank's end each pedal's body stands, as
      // tools/figure/mountain_bike.py builds it.
      constexpr meters_t pedal_out = 0.055f * u::m;

      // The rider: the pelvis this far above the saddle, the eyes this far
      // below the level, the arms reaching this share of their length
      // before the trunk leans in, the wrist this far back from its grip,
      // and the ankle above, behind, and inboard of the ball of the foot.
      constexpr meters_t seat_rise = 0.07f * u::m;
      constexpr radians_t gaze_down = 0.25f * u::rad;
      constexpr auto arm_stretch = 0.9f * proportion[one];
      constexpr meters_t wrist_short = 0.07f * u::m;
      constexpr meters_t ankle_rise = 0.07f * u::m;
      constexpr meters_t ankle_back = 0.08f * u::m;
      constexpr meters_t ankle_in = 0.03f * u::m;
      // The leaning search: from upright to the most, in steps.
      constexpr radians_t most_lean = 1.3f * u::rad;
      constexpr radians_t lean_step = 0.04f * u::rad;

      struct Meshes {
        render::MeshPtr frame;
        render::MeshPtr steering;
        render::MeshPtr lowers;
        render::MeshPtr wheel;
        render::MeshPtr cassette;
        render::MeshPtr crank;
        render::MeshPtr pedal;
      };

      const Meshes& meshes (render::Renderer& r) {
        static const Meshes m = [&r] {
          Meshes m;
          m.frame = model_mesh::bake (r, model::frame);
          m.steering = model_mesh::bake (r, model::steering);
          m.lowers = model_mesh::bake (r, model::fork_lowers);
          m.wheel = model_mesh::bake (r, model::wheel);
          m.cassette = model_mesh::bake (r, model::cassette);
          m.crank = model_mesh::bake (r, model::crank);
          m.pedal = model_mesh::bake (r, model::pedal);
          return m;
        }();
        return m;
      }

      // Model space is the chassis' own: x to the bike's right (the
      // drive side), y up, z forward, from the reference point midway
      // between the axles.
      Mat4 bike_frame (const VehiclePose& bike) {
        const direction_t forward = unit_direction (
          normalized (direction_value (bike.render_orientation)));
        const direction_t right = unit_direction (normalized (cross (
          direction_value (bike.render_normal), direction_value (forward))));
        const direction_t up = unit_direction (
          cross (direction_value (forward), direction_value (right)));
        return Mat4::translation (position_value (bike.position)) *
               Mat4::basis (direction_value (right),
                            direction_value (up),
                            direction_value (forward)) *
               Mat4::rotation (bike.lean, Vec3 (0, 0, 1)) *
               Mat4::scaling (Vec3 (1, 1, 1) *
                              bike.scale.numerical_value_in (one));
      }

      direction_t steering_axis () {
        return unit_direction (
          normalized (model::steering_top - model::steering_head));
      }

      // The bars and fork, turned about the head tube, from the crown.
      Mat4 steering_turn (const VehiclePose& bike, const Mat4& frame) {
        return frame * Mat4::translation (model::steering_head) *
               Mat4::rotation (-bike.fork, direction_value (steering_axis ()));
      }

      // Where each pedal's body is, as an offset in model space: the right
      // crank down at a crank angle of zero, the left one opposite it.
      displacement_t pedal_at (const VehiclePose& bike, int side) {
        const displacement_t arm =
          displacement (model::crank_end - model::bottom_bracket);
        // The arm turned about x as the crank mesh is turned: the
        // rotation's entries, lane by lane.
        const float c = std::cos (radians_value (bike.crank));
        const float s = std::sin (radians_value (bike.crank));
        const Vec3& a = displacement_value (arm);
        displacement_t out =
          displacement (Vec3 (a[0] + meters_value (pedal_out),
                              a[1] * c - a[2] * s,
                              a[1] * s + a[2] * c));
        if (side < 0)
          out = -out;
        return displacement (model::bottom_bracket) + out;
      }

      // The hiker in the saddle: hands on the grips, the balls of the feet
      // on the pedals, knees forward, leaning over the top tube just far
      // enough to reach the bars, eyes a little down the trail.
      AvatarSkeleton pose_rider (const VehiclePose& bike, const Mat4& frame) {
        const direction_t up =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 1, 0))));
        const direction_t facing =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 0, 1))));
        const direction_t right = unit_direction (
          normalized (cross (direction_value (up), direction_value (facing))));
        const Mat4 steering = steering_turn (bike, frame);
        const position_t grip[2] = {
          position (
            steering.transform_point (model::grip_left - model::steering_head)),
          position (
            steering.transform_point (model::grip_right - model::steering_head))
        };

        HoldPose hold;
        hold.facing = facing;
        hold.up = up;
        hold.pelvis = position (frame.transform_point (model::saddle) +
                                displacement_value (along (up, seat_rise)));
        hold.gaze =
          direction_value (facing) * std::cos (radians_value (gaze_down)) -
          direction_value (up) * std::sin (radians_value (gaze_down));
        hold.head_up = direction_value (up);
        hold.sole = facing;
        hold.instep = up;

        // The most upright lean whose shoulders reach the grips, or failing
        // that, the lean that comes closest.
        const meters_t reach =
          arm_stretch * (avatar_size::upper_arm + avatar_size::forearm);
        radians_t best_lean = 0.0f * u::rad;
        meters_t best_span = 1e9f * u::m;
        for (radians_t lean = 0.0f * u::rad; lean <= most_lean;
             lean += lean_step) {
          hold.lean = lean;
          const meters_t span =
            std::max (length (position_value (held_shoulder (hold, 0)) -
                              position_value (grip[0])),
                      length (position_value (held_shoulder (hold, 1)) -
                              position_value (grip[1]))) *
            u::m;
          if (span <= reach) {
            best_lean = lean;
            break;
          }
          if (span < best_span) {
            best_span = span;
            best_lean = lean;
          }
        }
        hold.lean = best_lean;

        for (int i = 0; i < 2; ++i) {
          const int side = i == 0 ? -1 : 1;
          const position_t shoulder = held_shoulder (hold, i);
          hold.wrist[i] =
            position (position_value (grip[i]) -
                      displacement_value (along (
                        direction_of (grip[i] - shoulder), wrist_short)));
          hold.elbow_pole[i] = direction_value (right) * side -
                               direction_value (up) * 0.4f -
                               direction_value (facing) * 0.2f;
          // The ankle stands above and behind the ball of the foot.
          const position_t pedal = position (frame.transform_point (
            displacement_value (pedal_at (bike, i == 0 ? -1 : 1))));
          hold.ankle[i] =
            position (position_value (pedal) -
                      displacement_value (along (right, side * ankle_in)) +
                      displacement_value (along (up, ankle_rise)) -
                      displacement_value (along (facing, ankle_back)));
          hold.knee_pole[i] = direction_value (facing) +
                              direction_value (up) * 0.2f +
                              direction_value (right) * (side * 0.12f);
        }
        return pose_holding (hold);
      }
    }
  }
  using namespace mountain_bike_drawing;

  void render_mountain_bike (render::Renderer& r,
                             render::DrawList& dl,
                             const VehiclePose& bike,
                             std::optional<Outfit> rider,
                             uint64_t motion_base) {
    const Meshes& m = meshes (r);
    const Mat4 frame = bike_frame (bike);
    if (rider)
      figure::draw (dl, pose_rider (bike, frame), *rider);

    uint64_t motion_part = motion_base;
    const auto draw_part = [&] (const render::Mesh& mesh,
                                const Mat4& transform) {
      r.draw_mesh (mesh, transform, ++motion_part);
    };
    const direction_t x_axis = unit_direction (Vec3 (1, 0, 0));
    const Mat4 spin =
      Mat4::rotation (bike.wheel_spin, direction_value (x_axis));

    draw_part (*m.frame, frame);

    // The rear wheel hangs on the frame, giving only as its tyre does; the
    // cassette turns with it.
    const displacement_t rear_axle =
      displacement (model::rear_axle) -
      displacement (Vec3 (0, meters_value (bike.rear_wheel_drop), 0));
    const Mat4 rear =
      frame * Mat4::translation (displacement_value (rear_axle)) * spin;
    draw_part (*m.wheel, rear);
    draw_part (*m.cassette, rear);

    // The fork: the crown, stanchions, and bars turn about the head tube,
    // and the lowers and the wheel slide along it as the fork works.
    const Mat4 steering = steering_turn (bike, frame);
    draw_part (*m.steering, steering);
    const direction_t axis = steering_axis ();
    const displacement_t axle =
      displacement (model::front_axle - model::steering_head) -
      along (axis, bike.front_wheel_drop / direction_value (axis)[1]);
    draw_part (*m.lowers,
               steering * Mat4::translation (displacement_value (axle)));
    draw_part (*m.wheel,
               steering * Mat4::translation (displacement_value (axle)) * spin);

    // The crank turns about the bottom bracket; each pedal stays level
    // on the end of its arm.
    draw_part (*m.crank,
               frame * Mat4::translation (model::bottom_bracket) *
                 Mat4::rotation (bike.crank, direction_value (x_axis)));
    for (int side : { -1, 1 })
      draw_part (
        *m.pedal,
        frame * Mat4::translation (displacement_value (pedal_at (bike, side))));
  }
}
