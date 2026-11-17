#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/jet_ski_model.hh>
#include <moppe/game/jet_ski_render.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    // The engine is a unity build; the other machines' drawing has
    // helpers of the same names.
    namespace jet_ski_drawing {
      struct JetSkiMeshes {
        render::MeshPtr hull;
        render::MeshPtr bars;
        render::MeshPtr nozzle;
      };

      const JetSkiMeshes& jet_ski_meshes (render::Renderer& r) {
        static const JetSkiMeshes meshes = [&r] {
          JetSkiMeshes m;
          m.hull = model_mesh::bake (r, jet_ski_model::hull);
          m.bars = model_mesh::bake (r, jet_ski_model::bars);
          m.nozzle = model_mesh::bake (r, jet_ski_model::nozzle);
          return m;
        }();
        return meshes;
      }

      // The bars turn about the column, which leans back toward the
      // rider, a little further than the nozzle swings: to the right
      // for a turn to the right.
      Mat4 bars_frame (const JetSkiPose& ski, const Mat4& frame) {
        const Vec3 axis =
          normalized (jet_ski_model::column_top - jet_ski_model::column);
        return frame * Mat4::translation (jet_ski_model::column) *
               Mat4::rotation (-1.3f * ski.steer_radians * u::rad, axis);
      }

      // The rider astride the saddle, a hand on each grip and a foot in
      // each well, their trunk forward over the cowl and leant further
      // into the turn than the hull banks.
      AvatarSkeleton pose_jet_ski_rider (const JetSkiPose& ski,
                                         const Mat4& frame,
                                         const Mat4& bars) {
        const Vec3 hull_up =
          normalized (frame.transform_vector (Vec3 (0, 1, 0)));
        const Vec3 facing =
          normalized (frame.transform_vector (Vec3 (0, 0, 1)));
        const Vec3 hull_left = normalized (cross (hull_up, facing));
        // Leaning to the right carries the head to starboard.
        const float lean = 0.6f * ski.lean_radians;
        const Vec3 up =
          normalized (hull_up * std::cos (lean) - hull_left * std::sin (lean));
        const Vec3 left = normalized (cross (up, facing));
        const Vec3 foot[2] = { jet_ski_model::foot_right,
                               jet_ski_model::foot_left };
        const Vec3 grip[2] = { jet_ski_model::grip_right,
                               jet_ski_model::grip_left };

        HoldPose hold;
        hold.facing = facing;
        hold.up = up;
        hold.pelvis =
          frame.transform_point (jet_ski_model::seat) + hull_up * 0.09f;
        hold.lean = 0.40f;
        // Looking where the turn goes.
        hold.gaze = normalized (facing + hull_up * 0.04f -
                                hull_left * (0.6f * ski.steer_radians));
        hold.head_up = hull_up;
        hold.sole = facing;
        hold.instep = hull_up;
        for (int i = 0; i < 2; ++i) {
          const float side = i == 0 ? -1.0f : 1.0f;
          const Vec3 hand =
            bars.transform_point (grip[i] - jet_ski_model::column);
          const Vec3 shoulder = held_shoulder (hold, i);
          hold.wrist[i] = hand - normalized (hand - shoulder) * 0.07f;
          hold.elbow_pole[i] = left * side - up * 0.5f - facing * 0.2f;
          hold.ankle[i] = frame.transform_point (foot[i]) + hull_up * 0.085f;
          hold.knee_pole[i] = up + facing * 0.7f + left * (side * 0.55f);
        }
        return pose_holding (hold);
      }

      // Riding pillion (MachineKind::passenger_seat) on the saddle's
      // back half: astride it behind the rider, feet in the wells
      // further aft, and hands at the rider's waist.
      AvatarSkeleton pose_jet_ski_pillion (const JetSkiPose& ski,
                                           const Mat4& frame) {
        const Vec3 hull_up =
          normalized (frame.transform_vector (Vec3 (0, 1, 0)));
        const Vec3 facing =
          normalized (frame.transform_vector (Vec3 (0, 0, 1)));
        const Vec3 hull_left = normalized (cross (hull_up, facing));
        const float lean = 0.5f * ski.lean_radians;
        const Vec3 up =
          normalized (hull_up * std::cos (lean) - hull_left * std::sin (lean));
        const Vec3 left = normalized (cross (up, facing));
        const Vec3 behind (0.0f, 0.03f, -0.5f);
        const Vec3 foot[2] = { jet_ski_model::foot_right + behind,
                               jet_ski_model::foot_left + behind };

        HoldPose hold;
        hold.facing = facing;
        hold.up = up;
        hold.pelvis = frame.transform_point (jet_ski_model::seat + behind) +
                      hull_up * 0.09f;
        hold.lean = 0.25f;
        hold.gaze = normalized (facing + hull_up * 0.08f);
        hold.head_up = hull_up;
        hold.sole = facing;
        hold.instep = hull_up;
        for (int i = 0; i < 2; ++i) {
          const float side = i == 0 ? -1.0f : 1.0f;
          hold.wrist[i] = frame.transform_point (
            jet_ski_model::seat + Vec3 (side * 0.17f, 0.30f, -0.05f));
          hold.elbow_pole[i] = left * side - up * 0.5f;
          hold.ankle[i] = frame.transform_point (foot[i]) + hull_up * 0.085f;
          hold.knee_pole[i] = up + facing * 0.7f + left * (side * 0.55f);
        }
        return pose_holding (hold);
      }
    }
  }
  using namespace jet_ski_drawing;

  void render_jet_ski (render::Renderer& r,
                       render::DrawList& dl,
                       const JetSkiPose& ski,
                       std::optional<Outfit> rider,
                       uint64_t motion_base,
                       std::optional<Outfit> pillion) {
    const JetSkiMeshes& meshes = jet_ski_meshes (r);
    const Mat4 frame =
      model_mesh::body_frame (ski.position, ski.forward, ski.up);
    const Mat4 bars = bars_frame (ski, frame);
    if (rider)
      figure::draw (dl, pose_jet_ski_rider (ski, frame, bars), *rider);
    if (pillion)
      figure::draw (dl, pose_jet_ski_pillion (ski, frame), *pillion);

    r.draw_mesh (*meshes.hull, frame, motion_base + 1);
    r.draw_mesh (*meshes.bars, bars, motion_base + 2);
    // The nozzle swings about its pivot's up: its mouth to starboard to
    // turn the bow to the right.
    r.draw_mesh (*meshes.nozzle,
                 frame * Mat4::translation (jet_ski_model::jet) *
                   Mat4::rotation (ski.steer_radians * u::rad, Vec3 (0, 1, 0)),
                 motion_base + 3);
  }
}
