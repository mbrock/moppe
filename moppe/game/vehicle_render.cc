#include <moppe/game/avatar.hh>
#include <moppe/game/bike_model.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/render/renderer.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    static degrees_t boost_nozzle_angle (const VehiclePose& vehicle) {
      // The exhaust points opposite the force: backward when boosting
      // forward, straight down at neutral drive, and forward in reverse.
      return (90.0f + 60.0f * vehicle.boost_drive) * u::deg;
    }

    // -- baked bike assemblies ------------------------------------------
    //
    // The bike is modelled in models/bike.blend as rigid assemblies, each
    // exported in its own frame (game/bike_model.*), recorded once into
    // retained meshes, and replayed with a model matrix. The suspension
    // moves rigid parts rather than stretching any: the swingarm swings,
    // the shock's halves turn to stay aimed at each other, and the fork
    // sliders telescope along the fork tubes.
    namespace {
      struct BikeMeshes {
        render::MeshPtr wheel;    // spoked wheel around the x axle
        render::MeshPtr chassis;  // rigid frame cluster in bike space
        render::MeshPtr steering; // clamp cluster about the steering head
        render::MeshPtr sliders;  // fork lowers about the front axle
        render::MeshPtr swingarm; // about its pivot
        render::MeshPtr shock_body;
        render::MeshPtr shock_shaft;
        render::MeshPtr nozzle; // one jump-jet bell along +z
      };

      render::MeshPtr bake (render::Renderer& r, const model_mesh::Mesh& mesh) {
        render::DrawList dl;
        model_mesh::record (dl, mesh);
        return r.create_mesh (dl);
      }

      const BikeMeshes& bike_meshes (render::Renderer& r) {
        static const BikeMeshes meshes = [&r] {
          BikeMeshes b;
          b.wheel = bake (r, bike_model::wheel);
          b.chassis = bake (r, bike_model::chassis);
          b.sliders = bake (r, bike_model::fork_sliders);
          b.swingarm = bake (r, bike_model::swingarm);
          b.shock_body = bake (r, bike_model::shock_body);
          b.shock_shaft = bake (r, bike_model::shock_shaft);
          b.nozzle = bake (r, bike_model::nozzle);

          // The headlight's lens is unlit in the model; a soft additive
          // halo around it is what the bloom pass picks up.
          render::DrawList dl;
          model_mesh::record (dl, bike_model::steering);
          render::DrawState glow;
          glow.blend = true;
          glow.additive = true;
          glow.depth_write = false;
          dl.push ();
          dl.translate (bike_model::headlight - bike_model::steering_head);
          dl.state (glow);
          dl.lit (false);
          dl.color (1.0f, 0.85f, 0.5f, 0.22f);
          dl.sphere (0.11f, 10, 10);
          dl.state (render::DrawState ());
          dl.lit (true);
          dl.pop ();
          b.steering = r.create_mesh (dl);
          return b;
        }();
        return meshes;
      }

      // -- baked flame cones --------------------------------------------
      //
      // Each additive layer is a unit cone (base radius 1 at z=0, apex
      // at z=1) with its color and glow state baked in; per frame only
      // the model matrix changes, stretching the cone with thrust and
      // engine flicker.  Unlit, so the non-uniform scale needs no
      // normal correction.
      struct FlameMeshes {
        render::MeshPtr lick_outer;   // exhaust flame, warm sheath
        render::MeshPtr lick_core;    // exhaust flame, pale core
        render::MeshPtr plume_sheath; // jump jet, wide warm wrap
        render::MeshPtr plume_body;   // jump jet, orange body
        render::MeshPtr plume_core;   // jump jet, white-hot core
      };

      render::MeshPtr record_flame_cone (render::Renderer& r,
                                         int slices,
                                         int stacks,
                                         float cr,
                                         float cg,
                                         float cb,
                                         float ca) {
        render::DrawList dl;
        render::DrawState glow;
        glow.blend = true;
        glow.additive = true;
        glow.depth_write = false;
        dl.state (glow);
        dl.lit (false);
        dl.color (cr, cg, cb, ca);
        dl.cone (1.0f, 1.0f, slices, stacks);
        return r.create_mesh (dl);
      }

      const FlameMeshes& flame_meshes (render::Renderer& r) {
        static const FlameMeshes meshes = [&r] {
          FlameMeshes f;
          f.lick_outer = record_flame_cone (r, 8, 2, 1.0f, 0.45f, 0.08f, 0.55f);
          f.lick_core = record_flame_cone (r, 8, 2, 1.0f, 0.85f, 0.45f, 0.75f);
          f.plume_sheath =
            record_flame_cone (r, 10, 3, 1.0f, 0.42f, 0.10f, 0.28f);
          f.plume_body =
            record_flame_cone (r, 10, 3, 1.0f, 0.62f, 0.18f, 0.55f);
          f.plume_core = record_flame_cone (r, 8, 3, 0.90f, 0.95f, 1.0f, 0.85f);
          return f;
        }();
        return meshes;
      }

      // The vehicle's model-to-world matrix, shared by render_vehicle's
      // matrix stack and the late flame pass so the two cannot drift.
      Mat4 vehicle_frame (const VehiclePose& vehicle) {
        const Vec3& pos = vehicle.position;
        const Vec3 fwd = normalized (vehicle.render_orientation);
        const Vec3 right = normalized (cross (vehicle.render_normal, fwd));
        const Vec3 up = cross (fwd, right);

        // Follow the smoothed surface frame on the ground and the velocity
        // arc in flight; lean into the corner.
        return Mat4::translation (pos) * Mat4::basis (right, up, fwd) *
               Mat4::rotation (vehicle.lean_radians * u::rad, Vec3 (0, 0, 1)) *
               Mat4::translation (Vec3 (0, 0.5f, 0)) *
               Mat4::scaling (Vec3 (1.5f, 1.5f, 1.5f));
      }
    }

    namespace {
      // The rotation about x carrying direction `from` to `to`, both in the
      // bike's yz plane.
      Mat4 turn_about_x (const Vec3& from, const Vec3& to) {
        const float angle =
          std::atan2 (to[2], to[1]) - std::atan2 (from[2], from[1]);
        return Mat4::rotation (angle * u::rad, Vec3 (1, 0, 0));
      }
    }

    namespace {
      // The bike is half again life size, as its physics is, and no
      // life-size rider could sit on it; the rider is drawn larger too,
      // scaled about the bike's origin.
      constexpr float rider_scale = 1.3f;

      // The hiker riding: seated on the saddle, feet on the pegs, hands on
      // the grips, the trunk leaning forward just far enough to reach the
      // bars, eyes a little down the trail. Posed in the bike's frame
      // shrunk by rider_scale, so the caller draws it scaled back up.
      AvatarSkeleton pose_rider (const VehiclePose& vehicle) {
        const Mat4 frame =
          Mat4::translation (vehicle.position) *
          Mat4::scaling (Vec3 (1, 1, 1) * (1.0f / rider_scale)) *
          Mat4::translation (-vehicle.position) * vehicle_frame (vehicle);
        const Vec3 up = normalized (frame.transform_vector (Vec3 (0, 1, 0)));
        const Vec3 facing =
          normalized (frame.transform_vector (Vec3 (0, 0, 1)));
        const Vec3 right = normalized (cross (up, facing));
        const Vec3 head = bike_model::steering_head;
        const Mat4 steering =
          frame * Mat4::translation (head) *
          Mat4::rotation (-vehicle.fork_radians * u::rad, Vec3 (0, 1, 0));
        const Vec3 grip[2] = {
          steering.transform_point (bike_model::grip_left - head),
          steering.transform_point (bike_model::grip_right - head)
        };
        const Vec3 peg[2] = { frame.transform_point (bike_model::peg_left),
                              frame.transform_point (bike_model::peg_right) };

        HoldPose hold;
        hold.facing = facing;
        hold.up = up;
        hold.pelvis = frame.transform_point (bike_model::saddle) + up * 0.1f;
        hold.gaze = facing * std::cos (0.2f) - up * std::sin (0.2f);
        hold.head_up = up;
        hold.sole = facing;
        hold.instep = up;

        // The most upright lean whose shoulders reach the grips, or failing
        // that, the lean that comes closest.
        const float reach =
          0.88f * (avatar_size::upper_arm + avatar_size::forearm);
        float best_lean = 0.0f, best_span = 1e9f;
        for (float lean = 0.0f; lean <= 1.2f; lean += 0.04f) {
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
          hold.elbow_pole[i] = right * side - up * 0.3f - facing * 0.3f;
          hold.ankle[i] = peg[i] + up * 0.085f - facing * 0.03f;
          hold.knee_pole[i] = facing + right * (side * 0.35f);
        }
        return pose_holding (hold);
      }
    }

    void render_vehicle (render::Renderer& r,
                         render::DrawList& dl,
                         const VehiclePose& vehicle,
                         bool ridden,
                         uint64_t motion_base) {
      if (ridden) {
        dl.push ();
        dl.translate (vehicle.position);
        dl.scale (rider_scale, rider_scale, rider_scale);
        dl.translate (-vehicle.position);
        figure::draw (dl, pose_rider (vehicle));
        dl.pop ();
      }

      const BikeMeshes& bm = bike_meshes (r);
      const Mat4 frame = vehicle_frame (vehicle);
      uint64_t motion_part = motion_base;
      const auto draw_part = [&] (const render::Mesh& mesh,
                                  const Mat4& transform) {
        r.draw_mesh (mesh, transform, ++motion_part);
      };

      const Vec3 x_axis (1, 0, 0), y_axis (0, 1, 0);
      const Mat4 spin =
        Mat4::rotation (vehicle.wheel_spin_radians * u::rad, x_axis);

      // Suspension: the pose places the frame and says how far each wheel
      // hangs from its rest position, so landings visibly compress the
      // travel.  (Model space is 2/3 world scale.)
      const float wheel_drop = -vehicle.rear_wheel_drop / 1.5f;
      const float fork_drop = -vehicle.front_wheel_drop / 1.5f;

      // The swingarm swings about its pivot until the rear axle sits
      // `wheel_drop` from rest, carrying the wheel and the shock's foot.
      const Vec3 pivot = bike_model::swingarm_pivot;
      const Vec3 arm = bike_model::rear_axle - pivot;
      const float reach = length (arm);
      const float arm_y =
        std::clamp (arm[1] + wheel_drop, -0.95f * reach, 0.95f * reach);
      const Vec3 swung (0, arm_y, -std::sqrt (reach * reach - arm_y * arm_y));
      const Mat4 swing = turn_about_x (arm, swung);
      const Mat4 swingarm = frame * Mat4::translation (pivot) * swing;
      draw_part (*bm.swingarm, swingarm);
      draw_part (*bm.wheel,
                 frame * Mat4::translation (pivot + swung) * swing * spin);

      // The shock's body hangs from the frame aimed at its foot on the
      // swingarm; the shaft rises from the foot aimed back at the body.
      const Vec3 top = bike_model::shock_top;
      const Vec3 foot =
        pivot + swing.transform_vector (bike_model::shock_bottom - pivot);
      const Mat4 aim =
        turn_about_x (bike_model::shock_bottom - top, foot - top);
      draw_part (*bm.shock_body, frame * Mat4::translation (top) * aim);
      draw_part (*bm.shock_shaft, frame * Mat4::translation (foot) * aim);

      // The rigid frame cluster: engine, tank, seat, fenders, exhaust.
      draw_part (*bm.chassis, frame);

      // Steering assembly: the clamp cluster turns about the steering
      // head, and the sliders and front wheel telescope along the forks.
      const radians_t steer = -vehicle.fork_radians * u::rad;
      const Vec3 head = bike_model::steering_head;
      const Mat4 steering =
        frame * Mat4::translation (head) * Mat4::rotation (steer, y_axis);
      draw_part (*bm.steering, steering);
      const Vec3 fork =
        normalized (bike_model::front_axle - bike_model::fork_top);
      const Vec3 axle =
        bike_model::front_axle - head + fork * (fork_drop / fork[1]);
      draw_part (*bm.sliders, steering * Mat4::translation (axle));
      draw_part (*bm.wheel, steering * Mat4::translation (axle) * spin);

      // Gimballed jump-jet nozzles under the frame.
      for (const Vec3& nozzle :
           { bike_model::nozzle_left, bike_model::nozzle_right })
        draw_part (*bm.nozzle,
                   frame * Mat4::translation (nozzle) *
                     Mat4::rotation (boost_nozzle_angle (vehicle), x_axis));
    }

    // The additive exhaust lick and jump-jet plumes, replayed as baked
    // unit cones under breathing scale matrices.  Called after the
    // world draw list plays so the glow blends over the solids, the
    // same reason the star halos draw last.
    void render_vehicle_flames (render::Renderer& r,
                                const VehiclePose& vehicle,
                                float time,
                                uint64_t motion_base) {
      const float thrust = std::abs (vehicle.thrust);
      const bool exhaust = thrust > 0.1f;
      const bool boosting = vehicle.boost_level > 0.001f;
      if (!exhaust && !boosting)
        return;

      const FlameMeshes& fm = flame_meshes (r);
      const Mat4 frame = vehicle_frame (vehicle);
      const Vec3 x_axis (1, 0, 0), y_axis (0, 1, 0);
      uint64_t motion_part = motion_base + 0x100;
      const auto draw_part = [&] (const render::Mesh& mesh,
                                  const Mat4& transform) {
        r.draw_mesh (mesh, transform, ++motion_part);
      };

      // Exhaust flame licking out of the muffler under load: an
      // additive two-layer lick with a pale core.
      if (exhaust) {
        const float lick =
          0.85f + 0.15f * std::sin (time * 47.0f + std::sin (time * 31.0f));
        const Mat4 muffler = frame *
                             Mat4::translation (bike_model::muffler_tip) *
                             Mat4::rotation (180 * u::deg, y_axis);
        draw_part (*fm.lick_outer,
                   muffler * Mat4::scaling (Vec3 (
                               0.07f, 0.07f, (0.16f + 0.30f * thrust) * lick)));
        draw_part (*fm.lick_core,
                   muffler *
                     Mat4::scaling (
                       Vec3 (0.035f, 0.035f, (0.22f + 0.36f * thrust) * lick)));
      }

      // Live jump-jet output: a layered additive plume -- a wide warm
      // sheath around an orange body around a white-hot core, all
      // shivering with engine flicker.  Additive layers sum where
      // they overlap, so the middle reads as incandescent.
      if (boosting) {
        const float k = vehicle.boost_level;
        const float flicker = 0.88f + 0.12f * std::sin (time * 41.0f) *
                                        std::sin (time * 27.0f + 1.7f);
        const float len = k * flicker;

        // Plume proportions; each starts inside its nozzle's bell.
        const float sheath_r = 0.21f;
        const float body_r = 0.12f;
        const float core_r = 0.055f;
        const float sheath_l = 1.9f;
        const float body_l = 2.5f;
        const float core_l = 3.0f;

        for (const Vec3& nozzle :
             { bike_model::nozzle_left, bike_model::nozzle_right }) {
          const Mat4 jet =
            frame * Mat4::translation (nozzle) *
            Mat4::rotation (boost_nozzle_angle (vehicle), x_axis) *
            Mat4::translation (Vec3 (0, 0, 0.1f));
          draw_part (
            *fm.plume_sheath,
            jet * Mat4::scaling (Vec3 (sheath_r, sheath_r, sheath_l * len)));
          draw_part (*fm.plume_body,
                     jet * Mat4::scaling (Vec3 (body_r, body_r, body_l * len)));
          draw_part (*fm.plume_core,
                     jet * Mat4::scaling (Vec3 (core_r, core_r, core_l * len)));
        }
      }
    }
  }
}
