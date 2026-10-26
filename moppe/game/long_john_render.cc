#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/long_john_model.hh>
#include <moppe/game/long_john_render.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/mov/rigid_bike.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    // The engine is a unity build, and the other vehicles' drawing has
    // helpers of the same names.
    namespace long_john_drawing {
      namespace model = long_john_model;

      // How far outboard of the crank's end each pedal's body stands, as
      // tools/figure/long_john.py takes it.
      constexpr float pedal_out = 0.065f;

      struct Meshes {
        render::MeshPtr frame;
        render::MeshPtr fork;
        render::MeshPtr bars;
        render::MeshPtr rear_wheel;
        render::MeshPtr front_wheel;
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
          m.fork = bake (r, model::fork);
          m.bars = bake (r, model::bars);
          m.rear_wheel = bake (r, model::rear_wheel);
          m.front_wheel = bake (r, model::front_wheel);
          m.crank = bake (r, model::crank);
          m.pedal = bake (r, model::pedal);
          return m;
        }();
        return m;
      }

      // Model space is the chassis' own: x to the bike's right (the
      // drive side), y up, z forward, from the reference point midway
      // between the axles at the rear one's height.
      Mat4 bike_frame (const VehiclePose& bike) {
        const Vec3 forward = normalized (bike.render_orientation);
        const Vec3 right = normalized (cross (bike.render_normal, forward));
        const Vec3 up = cross (forward, right);
        return Mat4::translation (bike.position) *
               Mat4::basis (right, up, forward) *
               Mat4::rotation (bike.lean_radians * u::rad, Vec3 (0, 0, 1)) *
               Mat4::scaling (Vec3 (1, 1, 1) * bike.scale);
      }

      // A head tube's steering, turned about its own axis from `head`.
      Mat4 steering_turn (const VehiclePose& bike,
                          const Mat4& frame,
                          const Vec3& head,
                          const Vec3& top) {
        return frame * Mat4::translation (head) *
               Mat4::rotation (-bike.fork_radians * u::rad,
                               normalized (top - head));
      }

      Mat4 bars_turn (const VehiclePose& bike, const Mat4& frame) {
        return steering_turn (
          bike, frame, model::bars_steering_head, model::bars_steering_top);
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

      // The hiker in the saddle over the back wheel, sitting up as on a
      // town bike: hands on the swept bars, the balls of the feet on the
      // pedals, eyes over the box to the trail.
      AvatarSkeleton pose_rider (const VehiclePose& bike, const Mat4& frame) {
        const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
        const Vec3 facing =
          normalized (frame.transform_vector (Vec3 (0, 0, 1)));
        const Vec3 right = normalized (cross (up, facing));
        const Mat4 bars = bars_turn (bike, frame);
        const Vec3 grip[2] = {
          bars.transform_point (model::grip_left - model::bars_steering_head),
          bars.transform_point (model::grip_right - model::bars_steering_head)
        };

        HoldPose hold;
        hold.facing = facing;
        hold.up = up;
        hold.pelvis = frame.transform_point (model::saddle) + up * 0.07f;
        hold.gaze = facing * std::cos (0.2f) - up * std::sin (0.2f);
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
          hold.elbow_pole[i] = right * side - up * 0.6f;
          // The ankle stands above and behind the ball of the foot.
          const Vec3 pedal =
            frame.transform_point (pedal_at (bike, i == 0 ? -1 : 1));
          hold.ankle[i] =
            pedal - right * (side * 0.04f) + up * 0.07f - facing * 0.08f;
          hold.knee_pole[i] = facing + up * 0.2f + right * (side * 0.15f);
        }
        return pose_holding (hold);
      }

      void draw (render::Renderer& r,
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
    // The small front wheel turns the faster for covering the same
    // ground as the rear.
    const float front_spin =
      bike.wheel_spin_radians * (mov::long_john_rig.wheel_radius /
                                 mov::long_john_rig.front_wheel_radius);

    draw_part (*m.frame, frame);

    // The rear wheel hangs on the frame, giving only as its tyre does.
    draw_part (*m.rear_wheel,
               frame *
                 Mat4::translation (model::rear_axle -
                                    Vec3 (0, bike.rear_wheel_drop, 0)) *
                 Mat4::rotation (bike.wheel_spin_radians * u::rad, x_axis));

    // The fork about the front head tube, carrying the front wheel, and
    // the bars about the rider's, turned alike by the cables.
    const Mat4 fork = steering_turn (bike,
                                     frame,
                                     model::front_steering_head,
                                     model::front_steering_top);
    draw_part (*m.fork, fork);
    const Vec3 axis =
      normalized (model::front_steering_top - model::front_steering_head);
    const Vec3 axle = model::front_axle - model::front_steering_head -
                      axis * (bike.front_wheel_drop / axis[1]);
    draw_part (*m.front_wheel,
               fork * Mat4::translation (axle) *
                 Mat4::rotation (front_spin * u::rad, x_axis));
    draw_part (*m.bars, bars_turn (bike, frame));

    // The crank turns about the bottom bracket; each pedal stays level
    // on the end of its arm.
    draw_part (*m.crank,
               frame * Mat4::translation (model::bottom_bracket) *
                 Mat4::rotation (bike.crank_radians * u::rad, x_axis));
    for (int side : { -1, 1 })
      draw_part (*m.pedal, frame * Mat4::translation (pedal_at (bike, side)));
      }
    }
  }

  void render_long_john (render::Renderer& r,
                         render::DrawList& dl,
                         const VehiclePose& bike,
                         std::optional<Outfit> rider,
                         uint64_t motion_base) {
    long_john_drawing::draw (r, dl, bike, rider, motion_base);
  }
}
