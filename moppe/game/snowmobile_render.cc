#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/fleet_render.hh>
#include <moppe/game/snowmobile_model.hh>
#include <moppe/gfx/mat4.hh>

#include <cmath>

namespace moppe::game {
  void render_snowmobile (const FleetScene& scene,
                          const CarPose& pose,
                          const Riders& who,
                          std::uint64_t base) {
    namespace m = snowmobile_model;
    auto& r = scene.r;
    static const auto body = model_mesh::bake (r, m::body),
                      track = model_mesh::bake (r, m::track),
                      cleat = model_mesh::bake (r, m::cleat),
                      glass = model_mesh::bake (r, m::glass),
                      bars = model_mesh::bake (r, m::bars),
                      left = model_mesh::bake (r, m::ski_left),
                      right = model_mesh::bake (r, m::ski_right);
    const Mat4 frame =
      model_mesh::body_frame (pose.position, pose.forward, pose.up) *
      Mat4::translation (
        Vec3 (0, -meters_value (mov::snowmobile_rig.wheel_radius), 0));
    const Mat4 handle =
      frame * Mat4::translation (m::column) *
      Mat4::rotation (-pose.steer, normalized (m::column_top - m::column));
    const Mat4 turn = Mat4::rotation (-pose.steer, Vec3 (0, 1, 0));
    r.draw_mesh (*body, frame, ++base);
    r.draw_mesh (*bars, handle, ++base);
    for (int i = 0; i < 2; ++i) {
      const displacement_t pivot =
        displacement (i ? m::ski_pivot_right : m::ski_pivot_left) -
        along (unit_direction (Vec3 (0, 1, 0)), pose.wheel_drop[i]);
      r.draw_mesh (i ? *right : *left,
                   frame * Mat4::translation (displacement_value (pivot)) *
                     turn,
                   ++base);
    }
    const Mat4 belt =
      frame *
      Mat4::translation (
        Vec3 (0,
              -meters_value ((pose.wheel_drop[2] + pose.wheel_drop[3]) * 0.5f),
              0));
    r.draw_mesh (*track, belt, ++base);
    // A stadium-shaped belt, bottom run first. Sixteen cleats pass per
    // revolution of the physics wheel, so its wrapped angle is seamless.
    constexpr meters_t span = 1.30f * u::m, radius = 0.16f * u::m;
    constexpr meters_t arc = PI * radius, loop = 2 * (span + arc);
    for (int i = 0; i < 40; ++i) {
      const auto cycle =
        (i - 16 * radians_value (pose.wheel_spin) / (2 * PI)) / 40;
      const meters_t s = loop * (cycle - std::floor (cycle));
      meters_t y {}, z {};
      radians_t pitch {};
      if (s < span)
        z = s;
      else if (s >= span + arc && s < 2 * span + arc) {
        z = 2 * span + arc - s;
        y = 2 * radius;
        pitch = -PI * u::rad;
      } else {
        const bool front = s < span + arc;
        const auto a = ((s - (front ? span : 2 * span + arc)) / radius)
                         .numerical_value_in (one) +
                       (front ? 0 : PI);
        z = (front ? span : meters_t {}) + radius * std::sin (a);
        y = radius * (1 - std::cos (a));
        pitch = -a * u::rad;
      }
      r.draw_mesh (
        *cleat,
        belt *
          Mat4::translation (m::cleat_origin +
                             Vec3 (0, meters_value (y), meters_value (z))) *
          Mat4::rotation (pitch, Vec3 (1, 0, 0)),
        ++base);
    }
    for (bool passenger : { false, true }) {
      const auto outfit = passenger ? who.passenger : who.driver;
      if (!outfit)
        continue;
      HoldPose hold;
      hold.up = hold.instep = pose.up;
      hold.facing = hold.sole = pose.forward;
      const Vec3 up = direction_value (pose.up),
                 forward = direction_value (pose.forward),
                 side = cross (up, forward);
      hold.head_up = up;
      hold.gaze = forward;
      hold.lean = (passenger ? 0.18f : 0.33f) * u::rad;
      hold.pelvis = position (
        frame.transform_point (passenger ? m::passenger_seat : m::seat) +
        up * 0.09f);
      for (int i = 0; i < 2; ++i) {
        const position_t foot = position (frame.transform_point (
          passenger ? (i ? m::passenger_foot_left : m::passenger_foot_right)
                    : (i ? m::foot_left : m::foot_right)));
        const position_t grip = position (
          passenger ? frame.transform_point (i ? m::passenger_grip_left
                                               : m::passenger_grip_right)
                    : handle.transform_point (
                        (i ? m::grip_left : m::grip_right) - m::column));
        const direction_t reach = direction_of (grip - held_shoulder (hold, i));
        hold.wrist[i] = grip - quantity_cast<isq::position_vector> (
                                 along (reach, 0.07f * u::m));
        hold.elbow_pole[i] = side * (i ? 1 : -1) - up * 0.5f;
        hold.ankle[i] = foot + quantity_cast<isq::position_vector> (
                                 along (pose.up, 0.085f * u::m));
        hold.knee_pole[i] = up + forward * 0.7f + side * (i ? 0.55f : -0.55f);
      }
      figure::draw (scene.dl, pose_holding (hold), *outfit);
    }
    r.draw_mesh (*glass, frame, ++base);
  }
}
