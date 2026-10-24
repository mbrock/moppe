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
      constexpr float pedal_out = 0.055f;

      struct Meshes {
        render::MeshPtr frame;
        render::MeshPtr steering;
        render::MeshPtr lowers;
        render::MeshPtr wheel;
        render::MeshPtr cassette;
        render::MeshPtr crank;
        render::MeshPtr pedal;
      };

      render::MeshPtr bake (render::Renderer& r, const model_mesh::Mesh& mesh) {
        render::DrawList dl;
        model_mesh::record (dl, mesh);
        return r.create_mesh (dl);
      }

      const Meshes& meshes (render::Renderer& r) {
        static const Meshes m = [&r] {
          Meshes m;
          m.frame = bake (r, model::frame);
          m.steering = bake (r, model::steering);
          m.lowers = bake (r, model::fork_lowers);
          m.wheel = bake (r, model::wheel);
          m.cassette = bake (r, model::cassette);
          m.crank = bake (r, model::crank);
          m.pedal = bake (r, model::pedal);
          return m;
        }();
        return m;
      }

      // Model space is the chassis' own: x to the bike's right (the
      // drive side), y up, z forward, from the reference point midway
      // between the axles.
      Mat4 bike_frame (const VehiclePose& bike) {
        const Vec3 forward = normalized (bike.render_orientation);
        const Vec3 right = normalized (cross (bike.render_normal, forward));
        const Vec3 up = cross (forward, right);
        return Mat4::translation (bike.position) *
               Mat4::basis (right, up, forward) *
               Mat4::rotation (bike.lean_radians * u::rad, Vec3 (0, 0, 1)) *
               Mat4::scaling (Vec3 (1, 1, 1) * bike.scale);
      }

      Vec3 steering_axis () {
        return normalized (model::steering_top - model::steering_head);
      }

      // The bars and fork, turned about the head tube, from the crown.
      Mat4 steering_turn (const VehiclePose& bike, const Mat4& frame) {
        return frame * Mat4::translation (model::steering_head) *
               Mat4::rotation (-bike.fork_radians * u::rad, steering_axis ());
      }

      // Where each pedal's body is, in model space: the right crank down
      // at a crank angle of zero, the left one opposite it.
      Vec3 pedal_at (const VehiclePose& bike, int side) {
        const Vec3 arm = model::crank_end - model::bottom_bracket;
        const float c = std::cos (bike.crank_radians);
        const float s = std::sin (bike.crank_radians);
        // The arm turned about x as the crank mesh is turned.
        Vec3 out (
          arm[0] + pedal_out, arm[1] * c - arm[2] * s, arm[1] * s + arm[2] * c);
        if (side < 0)
          out = Vec3 (-out[0], -out[1], -out[2]);
        return model::bottom_bracket + out;
      }

      // The hiker in the saddle: hands on the grips, the balls of the feet
      // on the pedals, knees forward, leaning over the top tube just far
      // enough to reach the bars, eyes a little down the trail.
      AvatarSkeleton pose_rider (const VehiclePose& bike, const Mat4& frame) {
        const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
        const Vec3 facing =
          normalized (frame.transform_vector (Vec3 (0, 0, 1)));
        const Vec3 right = normalized (cross (up, facing));
        const Mat4 steering = steering_turn (bike, frame);
        const Vec3 grip[2] = {
          steering.transform_point (model::grip_left - model::steering_head),
          steering.transform_point (model::grip_right - model::steering_head)
        };

        HoldPose hold;
        hold.facing = facing;
        hold.up = up;
        hold.pelvis = frame.transform_point (model::saddle) + up * 0.07f;
        hold.gaze = facing * std::cos (0.25f) - up * std::sin (0.25f);
        hold.head_up = up;
        hold.sole = facing;
        hold.instep = up;

        // The most upright lean whose shoulders reach the grips, or failing
        // that, the lean that comes closest.
        const float reach =
          0.9f * (avatar_size::upper_arm + avatar_size::forearm);
        float best_lean = 0.0f, best_span = 1e9f;
        for (float lean = 0.0f; lean <= 1.3f; lean += 0.04f) {
          hold.lean = lean;
          const float span =
            std::max (length (held_shoulder (hold, 0) - grip[0]),
                      length (held_shoulder (hold, 1) - grip[1]));
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
          const float side = i == 0 ? -1.0f : 1.0f;
          const Vec3 shoulder = held_shoulder (hold, i);
          hold.wrist[i] = grip[i] - normalized (grip[i] - shoulder) * 0.07f;
          hold.elbow_pole[i] = right * side - up * 0.4f - facing * 0.2f;
          // The ankle stands above and behind the ball of the foot.
          const Vec3 pedal =
            frame.transform_point (pedal_at (bike, i == 0 ? -1 : 1));
          hold.ankle[i] =
            pedal - right * (side * 0.03f) + up * 0.07f - facing * 0.08f;
          hold.knee_pole[i] = facing + up * 0.2f + right * (side * 0.12f);
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
    const Vec3 x_axis (1, 0, 0);
    const Mat4 spin = Mat4::rotation (bike.wheel_spin_radians * u::rad, x_axis);

    draw_part (*m.frame, frame);

    // The rear wheel hangs on the frame, giving only as its tyre does; the
    // cassette turns with it.
    const Mat4 rear =
      frame *
      Mat4::translation (model::rear_axle - Vec3 (0, bike.rear_wheel_drop, 0)) *
      spin;
    draw_part (*m.wheel, rear);
    draw_part (*m.cassette, rear);

    // The fork: the crown, stanchions, and bars turn about the head tube,
    // and the lowers and the wheel slide along it as the fork works.
    const Mat4 steering = steering_turn (bike, frame);
    draw_part (*m.steering, steering);
    const Vec3 axis = steering_axis ();
    const Vec3 axle = model::front_axle - model::steering_head -
                      axis * (bike.front_wheel_drop / axis[1]);
    draw_part (*m.lowers, steering * Mat4::translation (axle));
    draw_part (*m.wheel, steering * Mat4::translation (axle) * spin);

    // The crank turns about the bottom bracket; each pedal stays level
    // on the end of its arm.
    draw_part (*m.crank,
               frame * Mat4::translation (model::bottom_bracket) *
                 Mat4::rotation (bike.crank_radians * u::rad, x_axis));
    for (int side : { -1, 1 })
      draw_part (*m.pedal, frame * Mat4::translation (pedal_at (bike, side)));
  }
}
