#include <moppe/game/avatar.hh>
#include <moppe/game/caddy_model.hh>
#include <moppe/game/caddy_render.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/sling.hh>
#include <moppe/gfx/mat4.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    // The steering wheel turns this many times as far as the road wheels:
    // some three and a half turns from lock to lock.
    constexpr float caddy_steering_ratio = 4.6f;

    struct CaddyMeshes {
      render::MeshPtr body;
      render::MeshPtr glass;
      render::MeshPtr hardtop;
      render::MeshPtr tailgate;
      render::MeshPtr seats;
      render::MeshPtr column;
      render::MeshPtr steering_wheel;
      render::MeshPtr wheel;
    };

    render::MeshPtr bake_caddy_part (render::Renderer& r,
                                     const model_mesh::Mesh& mesh) {
      render::DrawList dl;
      model_mesh::record (dl, mesh);
      return r.create_mesh (dl);
    }

    const CaddyMeshes& caddy_meshes (render::Renderer& r) {
      static const CaddyMeshes meshes = [&r] {
        CaddyMeshes m;
        m.body = bake_caddy_part (r, caddy_model::body);
        m.glass = bake_caddy_part (r, caddy_model::glass);
        m.hardtop = bake_caddy_part (r, caddy_model::hardtop);
        m.tailgate = bake_caddy_part (r, caddy_model::tailgate);
        m.seats = bake_caddy_part (r, caddy_model::seats);
        m.column = bake_caddy_part (r, caddy_model::column);
        m.steering_wheel = bake_caddy_part (r, caddy_model::steering_wheel);
        m.wheel = bake_caddy_part (r, caddy_model::wheel);
        return m;
      }();
      return meshes;
    }

    Mat4 caddy_frame (const CaddyPose& caddy) {
      const Vec3 forward = normalized (caddy.forward);
      const Vec3 left = normalized (cross (caddy.up, forward));
      const Vec3 up = cross (forward, left);
      return Mat4::translation (caddy.position) *
             Mat4::basis (left, up, forward);
    }

    Mat4 caddy_steering_turn (const CaddyPose& caddy) {
      const Vec3 column = normalized (caddy_model::steering_column -
                                      caddy_model::steering_hub);
      return Mat4::rotation (
        caddy_steering_ratio * caddy.steer_radians * u::rad, column);
    }

    // The hiker in a front seat: the driver holding the wheel with feet
    // at the pedals, the passenger with hands on their knees.
    AvatarSkeleton caddy_seated (const CaddyPose& caddy,
                                 const Mat4& frame,
                                 bool driving) {
      const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
      const Vec3 facing = normalized (frame.transform_vector (Vec3 (0, 0, 1)));
      const Vec3 left = normalized (cross (up, facing));
      const Vec3 seat =
        driving ? caddy_model::seat_driver : caddy_model::seat_passenger;
      // The figure's first arm and leg are those on its -x side, which
      // is the car's right.
      const Vec3 foot[2] = {
        driving ? caddy_model::pedal_right : caddy_model::footrest_right,
        driving ? caddy_model::pedal_left : caddy_model::footrest_left
      };

      HoldPose hold;
      hold.facing = facing;
      hold.up = up;
      hold.pelvis = frame.transform_point (seat) + up * 0.09f;
      // Sat back into the seat, more upright than in the jeep.
      hold.lean = -0.10f;
      hold.gaze = facing;
      hold.head_up = up;
      hold.sole = normalized (facing + up * 0.45f);
      hold.instep = normalized (up - facing * 0.45f);

      const Mat4 wheel = frame *
                         Mat4::translation (caddy_model::steering_hub) *
                         caddy_steering_turn (caddy);
      const Vec3 grip[2] = { wheel.transform_point (caddy_model::grip_right -
                                                    caddy_model::steering_hub),
                             wheel.transform_point (caddy_model::grip_left -
                                                    caddy_model::steering_hub) };
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
        hold.knee_pole[i] = up + facing * 0.6f + left * (side * 0.15f);
      }
      return pose_holding (hold);
    }
  }

  void render_caddy (render::Renderer& r,
                     render::DrawList& dl,
                     const CaddyPose& caddy,
                     JeepRiders riders,
                     uint64_t motion_base) {
    const CaddyMeshes& meshes = caddy_meshes (r);
    const Mat4 frame = caddy_frame (caddy);
    if (riders.driver)
      figure::draw (
        dl, caddy_seated (caddy, frame, true), riders.driver_outfit);
    if (riders.passenger)
      figure::draw (
        dl, caddy_seated (caddy, frame, false), riders.passenger_outfit);

    uint64_t motion_part = motion_base;
    const auto draw_part = [&] (const render::Mesh& mesh,
                                const Mat4& transform) {
      r.draw_mesh (mesh, transform, ++motion_part);
    };
    draw_part (*meshes.body, frame);
    draw_part (*meshes.hardtop, frame);
    draw_part (*meshes.tailgate,
               frame * Mat4::translation (caddy_model::tailgate_hinge));
    draw_part (*meshes.seats, frame);
    draw_part (*meshes.column, frame);
    draw_part (*meshes.steering_wheel,
               frame * Mat4::translation (caddy_model::steering_hub) *
                 caddy_steering_turn (caddy));

    const Vec3 y_axis (0, 1, 0), x_axis (1, 0, 0);
    const Vec3 axles[4] = { caddy_model::wheel_fl,
                            caddy_model::wheel_fr,
                            caddy_model::wheel_rl,
                            caddy_model::wheel_rr };
    for (int wheel = 0; wheel < 4; ++wheel) {
      const bool left_side = wheel % 2 == 0;
      Mat4 turn = Mat4::identity ();
      if (wheel < 2)
        turn = Mat4::rotation (-caddy.steer_radians * u::rad, y_axis);
      // The model's wheel is a right-hand one; the left-hand ones are
      // turned about, which turns their roll about with them.
      if (left_side)
        turn = turn * Mat4::rotation (3.14159265f * u::rad, y_axis);
      const float roll =
        left_side ? -caddy.wheel_spin_radians : caddy.wheel_spin_radians;
      draw_part (*meshes.wheel,
                 frame *
                   Mat4::translation (axles[wheel] -
                                      Vec3 (0, caddy.wheel_drop[wheel], 0)) *
                   turn * Mat4::rotation (roll * u::rad, x_axis));
    }
    // The glass last, over what stands behind it.
    draw_part (*meshes.glass, frame);
  }

  namespace {
    // A rope as a square tube in a few straight pieces, hanging `sag`
    // metres at its middle.
    void draw_line (render::DrawList& dl,
                    const Vec3& from,
                    const Vec3& to,
                    float thick,
                    float sag) {
      constexpr int pieces = 10;
      const Vec3 along = to - from;
      const float span = length (along);
      if (span < 1e-3f)
        return;
      const Vec3 axis = along * (1.0f / span);
      Vec3 side = cross (axis, Vec3 (0, 1, 0));
      side = length2 (side) > 1e-6f ? normalized (side) : Vec3 (1, 0, 0);
      const Vec3 over = cross (side, axis);
      const auto at = [&] (int piece) {
        const float t = static_cast<float> (piece) / pieces;
        return from + along * t - Vec3 (0, sag * 4.0f * t * (1.0f - t), 0);
      };
      const Vec3 faces[4] = { side, over, -side, -over };
      for (int piece = 0; piece < pieces; ++piece) {
        const Vec3 a = at (piece), b = at (piece + 1);
        for (int f = 0; f < 4; ++f) {
          const Vec3 u = faces[f] * thick, v = faces[(f + 1) % 4] * thick;
          dl.normal (normalized (faces[f] + faces[(f + 1) % 4]));
          dl.vertex (a + u);
          dl.vertex (b + u);
          dl.vertex (b + v);
          dl.vertex (a + u);
          dl.vertex (b + v);
          dl.vertex (a + v);
        }
      }
    }
  }

  void render_sling (render::DrawList& dl, const SlingPose& sling) {
    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);
    // An orange lifting line, and the bridle's darker legs.
    dl.color (DisplayColor (0.86f, 0.42f, 0.10f));
    const float span = length (sling.hook - sling.ring);
    const float slack = std::max (0.0f, sling_line - span);
    draw_line (dl,
               sling.hook,
               sling.ring,
               0.035f,
               sling.taut ? 0.0f : std::min (0.5f * slack + 0.2f, 3.0f));
    dl.color (DisplayColor (0.30f, 0.30f, 0.28f));
    for (const Vec3& leg : sling.legs)
      draw_line (dl, sling.ring, leg, 0.025f, 0.0f);
    dl.end ();
    dl.state (render::DrawState ());
  }
}
