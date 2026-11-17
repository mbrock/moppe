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
      const direction_t fwd =
        unit_direction (normalized (direction_value (glider.heading)));
      const direction_t right = unit_direction (
        normalized (cross (Vec3 (0, 1, 0), direction_value (fwd))));
      const direction_t up =
        unit_direction (cross (direction_value (fwd), direction_value (right)));
      return Mat4::translation (position_value (glider.position)) *
             Mat4::basis (direction_value (right),
                          direction_value (up),
                          direction_value (fwd)) *
             Mat4::rotation (-glider.bank, Vec3 (0, 0, 1));
    }

    // How fast the pilot sways on the strap, and how far.
    constexpr auto sway_rate = 3.1f * u::rad / u::s;
    constexpr meters_t sway_reach = 0.035f * u::m;
    // The harness hangs the pelvis this far below and behind its eye.
    constexpr meters_t harness_drop = 0.12f * u::m;
    constexpr meters_t harness_aft = 0.08f * u::m;
    // The chest lifts the trunk back from the prone line.
    constexpr radians_t chest_lift = -0.2f * u::rad;
    // The trailing legs: ankles this far behind the pelvis and above,
    // and apart.
    constexpr meters_t legs_trail = 0.84f * u::m;
    constexpr meters_t legs_rise = 0.03f * u::m;
    constexpr meters_t legs_apart = 0.1f * u::m;
    // A wrist stands this far back from its grip.
    constexpr meters_t wrist_short = 0.07f * u::m;

    // The pilot lies prone in the harness, belly to the ground and head
    // toward the nose, chest lifted to look ahead, hands on the basebar
    // with the elbows out, legs trailing with the toes pointed; the body
    // sways a little on its strap.
    AvatarSkeleton pose_pilot (const Mat4& frame, seconds_t time) {
      const direction_t forward =
        unit_direction (normalized (frame.transform_vector (Vec3 (0, 0, 1))));
      const direction_t up =
        unit_direction (normalized (frame.transform_vector (Vec3 (0, 1, 0))));
      const direction_t right = unit_direction (
        normalized (cross (direction_value (up), direction_value (forward))));
      const meters_t sway =
        std::sin (radians_value (time * sway_rate)) * sway_reach;
      const position_t harness =
        position (frame.transform_point (glider_model::harness));

      HoldPose hold;
      hold.facing = -up;
      hold.up = forward;
      hold.pelvis =
        position (position_value (harness) -
                  displacement_value (along (up, harness_drop)) -
                  displacement_value (along (forward, harness_aft)) +
                  displacement_value (along (right, sway)));
      hold.lean = chest_lift;
      hold.gaze =
        normalized (direction_value (forward) - direction_value (up) * 0.15f);
      hold.head_up = direction_value (up);
      hold.sole = -forward;
      hold.instep = -up;
      const position_t grip[2] = {
        position (frame.transform_point (glider_model::grip_left)),
        position (frame.transform_point (glider_model::grip_right))
      };
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? -1 : 1;
        const position_t shoulder = held_shoulder (hold, i);
        const direction_t reach = direction_of (grip[i] - shoulder);
        hold.wrist[i] =
          position (position_value (grip[i]) -
                    displacement_value (along (reach, wrist_short)));
        hold.elbow_pole[i] =
          direction_value (right) * (side * 0.8f) - direction_value (up) * 0.5f;
        hold.ankle[i] =
          position (position_value (hold.pelvis) -
                    displacement_value (along (forward, legs_trail)) +
                    displacement_value (along (up, legs_rise)) +
                    displacement_value (along (right, side * legs_apart)));
        hold.knee_pole[i] = -direction_value (up);
      }
      return pose_holding (hold);
    }
  }

  void render_glider (render::Renderer& r,
                      render::DrawList& dl,
                      const GliderPose& glider,
                      seconds_t time,
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
