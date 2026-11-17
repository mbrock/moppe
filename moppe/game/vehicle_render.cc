#include <moppe/game/avatar.hh>
#include <moppe/game/bike_model.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/sprites.hh>
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
      return 90.0f * u::deg + 60.0f * u::deg * vehicle.boost_drive;
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

      const BikeMeshes& bike_meshes (render::Renderer& r) {
        static const BikeMeshes meshes = [&r] {
          BikeMeshes b;
          b.wheel = model_mesh::bake (r, bike_model::wheel);
          b.chassis = model_mesh::bake (r, bike_model::chassis);
          b.sliders = model_mesh::bake (r, bike_model::fork_sliders);
          b.swingarm = model_mesh::bake (r, bike_model::swingarm);
          b.shock_body = model_mesh::bake (r, bike_model::shock_body);
          b.shock_shaft = model_mesh::bake (r, bike_model::shock_shaft);
          b.nozzle = model_mesh::bake (r, bike_model::nozzle);

          // The headlight's lens is unlit in the model; a soft additive
          // halo around it is what the bloom pass picks up.
          render::DrawList dl;
          model_mesh::record (dl, bike_model::steering);
          render::DrawState glow;
          glow.blend = true;
          glow.additive = true;
          glow.depth_write = false;
          dl.push ();
          dl.translate (
            displacement (bike_model::headlight - bike_model::steering_head));
          dl.state (glow);
          dl.lit (false);
          dl.color (DisplayColor (1.0f, 0.85f, 0.5f), 0.22f * proportion[one]);
          dl.sphere (0.11f * u::m, 10, 10);
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
        render::MeshPtr lick_outer; // exhaust flame, warm sheath
        render::MeshPtr lick_core;  // exhaust flame, pale core
      };

      render::MeshPtr record_flame_cone (render::Renderer& r,
                                         int slices,
                                         int stacks,
                                         DisplayColor colour,
                                         proportion_t opacity) {
        render::DrawList dl;
        render::DrawState glow;
        glow.blend = true;
        glow.additive = true;
        glow.depth_write = false;
        dl.state (glow);
        dl.lit (false);
        dl.color (colour, opacity);
        dl.cone (1.0f * u::m, 1.0f * u::m, slices, stacks);
        return r.create_mesh (dl);
      }

      const FlameMeshes& flame_meshes (render::Renderer& r) {
        static const FlameMeshes meshes = [&r] {
          FlameMeshes f;
          f.lick_outer = record_flame_cone (r,
                                            8,
                                            2,
                                            DisplayColor (1.0f, 0.45f, 0.08f),
                                            0.55f * proportion[one]);
          f.lick_core = record_flame_cone (r,
                                           8,
                                           2,
                                           DisplayColor (1.0f, 0.85f, 0.45f),
                                           0.75f * proportion[one]);
          return f;
        }();
        return meshes;
      }

      // The vehicle's model-to-world matrix, shared by render_vehicle's
      // matrix stack and the late flame pass so the two cannot drift.
      Mat4 vehicle_frame (const VehiclePose& vehicle) {
        const direction_t fwd = unit_direction (
          normalized (direction_value (vehicle.render_orientation)));
        const direction_t right = unit_direction (normalized (cross (
          direction_value (vehicle.render_normal), direction_value (fwd))));
        const direction_t up = unit_direction (
          cross (direction_value (fwd), direction_value (right)));

        // Follow the smoothed surface frame on the ground and the velocity
        // arc in flight; lean into the corner. The Trail 90 is modelled at
        // life size about the point midway between its axles.
        return Mat4::translation (position_value (vehicle.position)) *
               Mat4::basis (direction_value (right),
                            direction_value (up),
                            direction_value (fwd)) *
               Mat4::rotation (vehicle.lean, Vec3 (0, 0, 1)) *
               Mat4::scaling (Vec3 (1, 1, 1) *
                              vehicle.scale.numerical_value_in (one));
      }
    }

    namespace {
      // The rotation about x carrying offset `from`'s direction to `to`'s,
      // both in the bike's yz plane.
      Mat4 turn_about_x (const displacement_t& from, const displacement_t& to) {
        const Vec3& f = displacement_value (from);
        const Vec3& t = displacement_value (to);
        const radians_t angle =
          (std::atan2 (t[2], t[1]) - std::atan2 (f[2], f[1])) * u::rad;
        return Mat4::rotation (angle, Vec3 (1, 0, 0));
      }

      // An offset as a move of a place: position_t is rooted in
      // isq::position_vector (docs/typed-quantities.md).
      auto as_move (const displacement_t& offset) {
        return quantity_cast<isq::position_vector> (offset);
      }
    }

    namespace {
      // The hiker riding the Trail 90: seated on the saddle, feet on the
      // pegs, hands on the high bars, the trunk leaning just far enough to
      // reach them, eyes a little down the trail. Leaning back for a
      // wheelie the rider slides back on the saddle and hangs from the
      // bars on straight arms, trunk upright against the bike's rise, eyes
      // on the way ahead.
      AvatarSkeleton pose_rider (const VehiclePose& vehicle) {
        // The rider's measures: the pelvis above the saddle (less when
        // leaning back) and behind it as far as the lean goes, the eyes
        // below the level and, in a wheelie, below the way ahead, the
        // arms' reach as a share of their length, how far back the
        // trunk's search may start, the wrist back from its grip, and the
        // ankle above and behind the peg.
        constexpr meters_t seat_rise = 0.1f * u::m;
        constexpr meters_t seat_drop_back = 0.04f * u::m;
        constexpr meters_t seat_slide = 0.2f * u::m;
        constexpr radians_t gaze_down = 0.2f * u::rad;
        constexpr radians_t ahead_down = 0.15f * u::rad;
        constexpr auto arm_stretch = 0.88f * proportion[one];
        constexpr radians_t lean_back_most = -0.5f * u::rad;
        constexpr radians_t most_lean = 1.2f * u::rad;
        constexpr radians_t lean_step = 0.04f * u::rad;
        constexpr meters_t wrist_short = 0.07f * u::m;
        constexpr meters_t ankle_rise = 0.085f * u::m;
        constexpr meters_t ankle_back = 0.03f * u::m;

        const Mat4 frame = vehicle_frame (vehicle);
        const direction_t up =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 1, 0))));
        const direction_t facing =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 0, 1))));
        const direction_t right = unit_direction (
          normalized (cross (direction_value (up), direction_value (facing))));
        const Vec3& up_vec = direction_value (up);
        const Vec3& facing_vec = direction_value (facing);
        const Vec3& right_vec = direction_value (right);
        const displacement_t head = displacement (bike_model::steering_head);
        const Mat4 steering = frame *
                              Mat4::translation (displacement_value (head)) *
                              Mat4::rotation (-vehicle.fork, Vec3 (0, 1, 0));
        const position_t grip[2] = {
          position (steering.transform_point (
            displacement_value (displacement (bike_model::grip_left) - head))),
          position (steering.transform_point (
            displacement_value (displacement (bike_model::grip_right) - head)))
        };
        const position_t peg[2] = {
          position (frame.transform_point (bike_model::peg_left)),
          position (frame.transform_point (bike_model::peg_right))
        };

        // In a wheelie the trunk keeps nearer the world's upright than the
        // bike's as the bike rears, the pelvis goes back off the saddle (or
        // forward, balancing), and the eyes stay on the way ahead.
        const control_signal_t back = std::clamp (vehicle.rider_lean,
                                                  -1.0f * control_signal[one],
                                                  1.0f * control_signal[one]);
        const control_signal_t back_only =
          std::max (back, 0.0f * control_signal[one]);
        const blend_weight_t posture =
          std::clamp (
            4.0f * std::abs (back.numerical_value_in (one)), 0.0f, 1.0f) *
          blend_weight[one];
        const radians_t rearing =
          std::clamp (
            std::asin (std::clamp (facing_vec[1], -1.0f, 1.0f)), 0.0f, 1.3f) *
          u::rad;
        const radians_t rear =
          posture.numerical_value_in (one) * 0.85f * rearing;
        HoldPose hold;
        hold.facing =
          unit_direction (facing_vec * std::cos (radians_value (rear)) +
                          up_vec * std::sin (radians_value (rear)));
        hold.up = unit_direction (up_vec * std::cos (radians_value (rear)) -
                                  facing_vec * std::sin (radians_value (rear)));
        const meters_t rise = seat_rise - seat_drop_back * back_only;
        const meters_t slide = seat_slide * back;
        hold.pelvis = position (frame.transform_point (bike_model::saddle) +
                                displacement_value (along (up, rise)) -
                                displacement_value (along (facing, slide)));
        hold.gaze = facing_vec * std::cos (radians_value (gaze_down)) -
                    up_vec * std::sin (radians_value (gaze_down));
        hold.head_up = up_vec;
        // The way ahead on the level, if the bike faces any way but up.
        const Vec3 level (facing_vec[0], 0.0f, facing_vec[2]);
        if (posture > 0.0f * blend_weight[one] && length2 (level) > 1e-4f) {
          const Vec3 ahead =
            normalized (level) * std::cos (radians_value (ahead_down)) -
            Vec3 (0, std::sin (radians_value (ahead_down)), 0);
          const float w = posture.numerical_value_in (one);
          hold.gaze = normalized (hold.gaze + (ahead - hold.gaze) * w);
          hold.head_up = normalized (up_vec + (Vec3 (0, 1, 0) - up_vec) * w);
        }
        hold.sole = unit_direction (facing_vec);
        hold.instep = unit_direction (up_vec);

        // The most upright lean whose shoulders reach the grips, or failing
        // that, the lean that comes closest.
        const meters_t reach =
          arm_stretch * (avatar_size::upper_arm + avatar_size::forearm);
        // Leaning back, the search starts from behind the upright, so the
        // trunk hangs back as far as straight arms let it.
        const radians_t start =
          lean_back_most * back_only.numerical_value_in (one);
        radians_t best_lean = 0.0f * u::rad;
        meters_t best_span = 1e9f * u::m;
        for (radians_t lean = start; lean <= most_lean; lean += lean_step) {
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
          hold.elbow_pole[i] =
            right_vec * side - up_vec * 0.3f - facing_vec * 0.3f;
          hold.ankle[i] =
            position (position_value (peg[i]) +
                      displacement_value (along (up, ankle_rise)) -
                      displacement_value (along (facing, ankle_back)));
          hold.knee_pole[i] = facing_vec + right_vec * (side * 0.35f);
        }
        return pose_holding (hold);
      }
    }

    void render_vehicle (render::Renderer& r,
                         render::DrawList& dl,
                         const VehiclePose& vehicle,
                         bool ridden,
                         uint64_t motion_base,
                         Outfit outfit) {
      if (ridden)
        figure::draw (dl, pose_rider (vehicle), outfit);

      const BikeMeshes& bm = bike_meshes (r);
      const Mat4 frame = vehicle_frame (vehicle);
      uint64_t motion_part = motion_base;
      const auto draw_part = [&] (const render::Mesh& mesh,
                                  const Mat4& transform) {
        r.draw_mesh (mesh, transform, ++motion_part);
      };
      const auto at = [] (const displacement_t& offset) {
        return Mat4::translation (displacement_value (offset));
      };

      const direction_t x_axis = unit_direction (Vec3 (1, 0, 0)),
                        y_axis = unit_direction (Vec3 (0, 1, 0));
      const Mat4 spin =
        Mat4::rotation (vehicle.wheel_spin, direction_value (x_axis));

      // Suspension: the pose places the frame and says how far each wheel
      // hangs from its rest position, so landings visibly compress the
      // travel.
      const meters_t wheel_drop = -vehicle.rear_wheel_drop;
      const meters_t fork_drop = -vehicle.front_wheel_drop;

      // The swingarm swings about its pivot until the rear axle sits
      // `wheel_drop` from rest, carrying the wheel and the shock's foot.
      const displacement_t pivot = displacement (bike_model::swingarm_pivot);
      const displacement_t arm = displacement (bike_model::rear_axle) - pivot;
      const meters_t reach = length (displacement_value (arm)) * u::m;
      const meters_t arm_y =
        std::clamp (displacement_value (arm)[1] * u::m + wheel_drop,
                    -0.95f * reach,
                    0.95f * reach);
      const meters_t arm_z = -sqrt (reach * reach - arm_y * arm_y);
      const displacement_t swung =
        displacement (Vec3 (0, meters_value (arm_y), meters_value (arm_z)));
      const Mat4 swing = turn_about_x (arm, swung);
      const Mat4 swingarm = frame * at (pivot) * swing;
      draw_part (*bm.swingarm, swingarm);
      draw_part (*bm.wheel, frame * at (pivot + swung) * swing * spin);

      // The shock's body hangs from the frame aimed at its foot on the
      // swingarm; the shaft rises from the foot aimed back at the body.
      const displacement_t top = displacement (bike_model::shock_top);
      const displacement_t bottom = displacement (bike_model::shock_bottom);
      const displacement_t foot =
        pivot + displacement (
                  swing.transform_vector (displacement_value (bottom - pivot)));
      const Mat4 aim = turn_about_x (bottom - top, foot - top);
      draw_part (*bm.shock_body, frame * at (top) * aim);
      draw_part (*bm.shock_shaft, frame * at (foot) * aim);

      // The rigid frame cluster: engine, tank, seat, fenders, exhaust.
      draw_part (*bm.chassis, frame);

      // Steering assembly: the clamp cluster turns about the steering
      // head, and the sliders and front wheel telescope along the forks.
      const radians_t steer = -vehicle.fork;
      const displacement_t head = displacement (bike_model::steering_head);
      const Mat4 steering =
        frame * at (head) * Mat4::rotation (steer, direction_value (y_axis));
      draw_part (*bm.steering, steering);
      const direction_t fork = unit_direction (
        normalized (bike_model::front_axle - bike_model::fork_top));
      const displacement_t axle =
        displacement (bike_model::front_axle) - head +
        along (fork, fork_drop / direction_value (fork)[1]);
      draw_part (*bm.sliders, steering * at (axle));
      draw_part (*bm.wheel, steering * at (axle) * spin);

      // Gimballed jump-jet nozzles under the frame.
      for (const displacement_t& nozzle :
           { displacement (bike_model::nozzle_left),
             displacement (bike_model::nozzle_right) })
        draw_part (*bm.nozzle,
                   frame * at (nozzle) *
                     Mat4::rotation (boost_nozzle_angle (vehicle),
                                     direction_value (x_axis)));
    }

    namespace {
      // One soft additive sprite about `centre`, stretched `half_length`
      // along `axis` as the camera sees it and `half_width` across.
      void jet_sprite (render::DrawList& dl,
                       const position_t& camera,
                       const position_t& centre,
                       const direction_t& axis,
                       meters_t half_length,
                       meters_t half_width,
                       DisplayColor colour,
                       proportion_t opacity) {
        const direction_t view = direction_of (camera - centre);
        Vec3 across = cross (direction_value (axis), direction_value (view));
        if (length2 (across) < 1e-8f)
          across = cross (Vec3 (0, 1, 0), direction_value (view));
        const direction_t side = unit_direction (normalized (across));
        const direction_t up = unit_direction (
          cross (direction_value (view), direction_value (side)));
        // How nearly the camera looks down the jet.
        const proportion_t end_on =
          std::fabs (dot (direction_value (axis), direction_value (view))) *
          proportion[one];
        const meters_t along_view =
          half_length + (half_width - half_length) * end_on * end_on;
        const displacement_t u = along (up, along_view),
                             v = along (side, half_width);
        dl.color (colour, opacity);
        dl.uv (0, 0);
        dl.vertex (centre - as_move (u) - as_move (v));
        dl.uv (1, 0);
        dl.vertex (centre - as_move (u) + as_move (v));
        dl.uv (1, 1);
        dl.vertex (centre + as_move (u) + as_move (v));
        dl.uv (0, 1);
        dl.vertex (centre + as_move (u) - as_move (v));
      }

      void render_jets (render::Renderer& r,
                        const VehiclePose& vehicle,
                        const Mat4& frame,
                        const position_t& camera,
                        seconds_t time,
                        uint64_t motion_id) {
        // The jet's mouth, this far out along the nozzle's bell, and how
        // fast its flame flickers, wobbles, and its diamonds pulse.
        constexpr meters_t mouth_out = 0.08f * u::m;
        constexpr auto flicker_fast = 53.0f * u::rad / u::s;
        constexpr auto flicker_slow = 31.0f * u::rad / u::s;
        constexpr auto wobble_rate = 37.0f * u::rad / u::s;
        constexpr auto pulse_rate = 61.0f * u::rad / u::s;

        static const render::TexturePtr disc = make_soft_disc_texture (r);
        static render::DrawList dl;
        dl.clear ();
        render::DrawState glow;
        glow.blend = true;
        glow.additive = true;
        glow.depth_write = false;
        glow.cull = false;
        dl.state (glow);
        dl.lit (false);
        dl.fogged (true);
        dl.set_texture (disc.get ());
        dl.begin (render::Prim::Quads);

        const proportion_t k = vehicle.boost_level;
        const direction_t x_axis = unit_direction (Vec3 (1, 0, 0));
        for (const displacement_t& nozzle :
             { displacement (bike_model::nozzle_left),
               displacement (bike_model::nozzle_right) }) {
          const Mat4 jet = frame *
                           Mat4::translation (displacement_value (nozzle)) *
                           Mat4::rotation (boost_nozzle_angle (vehicle),
                                           direction_value (x_axis));
          const position_t mouth = position (
            jet.transform_point (Vec3 (0, 0, meters_value (mouth_out))));
          const direction_t axis =
            unit_direction (normalized (jet.transform_vector (Vec3 (0, 0, 1))));
          // Each jet flickers on its own phase.
          const radians_t phase =
            displacement_value (nozzle)[0] > 0 ? 0.0f * u::rad : 2.1f * u::rad;
          const scale_factor_t flicker =
            (0.85f +
             0.15f * std::sin (radians_value (time * flicker_fast + phase)) *
               std::sin (radians_value (time * flicker_slow + 1.3f * phase))) *
            scale_factor[one];
          const meters_t length =
            (1.4f * u::m + 2.6f * u::m * k) * flicker.numerical_value_in (one);

          // The heat round the mouth, and the white-hot throat.
          jet_sprite (dl,
                      camera,
                      mouth,
                      axis,
                      0.34f * u::m,
                      0.34f * u::m,
                      DisplayColor (1.0f, 0.55f, 0.20f),
                      0.35f * k);
          jet_sprite (dl,
                      camera,
                      mouth + as_move (along (axis, 0.10f * u::m)),
                      axis,
                      0.20f * u::m,
                      0.11f * u::m,
                      DisplayColor (0.85f, 0.92f, 1.0f),
                      0.95f * k);

          // The body: blobs cooling and widening down the jet.
          constexpr int blobs = 7;
          for (int i = 0; i < blobs; ++i) {
            // How far down the jet, as a share of its length.
            const proportion_t t = (i + 0.5f) / blobs * proportion[one];
            const meters_t wobble =
              0.03f * u::m *
              std::sin (
                radians_value (time * wobble_rate + 5.0f * u::rad * t + phase));
            const position_t centre =
              mouth + as_move (along (axis, t * length)) +
              as_move (
                displacement (cross (direction_value (axis), Vec3 (0, 1, 0)) *
                              meters_value (wobble * t)));
            const DisplayColor colour = t < 0.35f * proportion[one]
                                          ? DisplayColor (1.0f, 0.86f, 0.52f)
                                        : t < 0.70f * proportion[one]
                                          ? DisplayColor (1.0f, 0.52f, 0.16f)
                                          : DisplayColor (0.95f, 0.26f, 0.08f);
            const proportion_t fade =
              (1.0f * proportion[one] - t) * (1.0f * proportion[one] - t);
            jet_sprite (dl,
                        camera,
                        centre,
                        axis,
                        0.75f * length / blobs + 0.12f * u::m,
                        0.08f * u::m + 0.20f * u::m * t,
                        colour,
                        (0.25f * proportion[one] + 0.55f * fade) * k);
          }

          // The core: narrow and near white, burning through the first
          // half, so the additive layers sum past what bloom picks up.
          constexpr int cores = 4;
          for (int i = 0; i < cores; ++i) {
            const proportion_t t = (i + 0.5f) / cores * 0.45f * proportion[one];
            jet_sprite (dl,
                        camera,
                        mouth + as_move (along (axis, t * length)),
                        axis,
                        0.16f * length / cores + 0.08f * u::m,
                        0.05f * u::m + 0.06f * u::m * t,
                        DisplayColor (1.0f, 0.95f, 0.80f),
                        (0.85f * proportion[one] - 0.8f * t) * k);
          }

          // Shock diamonds: knots that pulse where the jet re-compresses.
          for (int i = 0; i < 3; ++i) {
            const proportion_t t =
              0.16f * proportion[one] + 0.15f * proportion[one] * i;
            const proportion_t pulse =
              (0.65f +
               0.35f * std::sin (radians_value (time * pulse_rate +
                                                2.4f * u::rad * i + phase))) *
              proportion[one];
            jet_sprite (dl,
                        camera,
                        mouth + as_move (along (axis, t * length)),
                        axis,
                        0.10f * u::m,
                        0.075f * u::m - 0.012f * u::m * i,
                        DisplayColor (1.0f, 0.96f, 0.86f),
                        (0.9f * proportion[one] - 0.2f * proportion[one] * i) *
                          pulse * k);
          }
        }
        dl.end ();
        dl.set_texture (nullptr);
        dl.state (render::DrawState ());
        r.draw_list (dl, motion_id);
      }
    }

    // The additive exhaust lick and jump-jet plumes, replayed as baked
    // unit cones under breathing scale matrices.  Called after the
    // world draw list plays so the glow blends over the solids, the
    // same reason the star halos draw last.
    void render_vehicle_flames (render::Renderer& r,
                                const VehiclePose& vehicle,
                                const position_t& camera,
                                seconds_t time,
                                uint64_t motion_base) {
      // How fast the exhaust's lick breathes, and how wide its layers are.
      constexpr auto lick_fast = 47.0f * u::rad / u::s;
      constexpr auto lick_slow = 31.0f * u::rad / u::s;
      constexpr meters_t outer_radius = 0.07f * u::m;
      constexpr meters_t core_radius = 0.035f * u::m;

      const control_signal_t thrust = abs (vehicle.thrust);
      const bool exhaust = thrust > 0.1f * control_signal[one];
      const bool boosting = vehicle.boost_level > 0.001f * proportion[one];
      if (!exhaust && !boosting)
        return;

      const FlameMeshes& fm = flame_meshes (r);
      const Mat4 frame = vehicle_frame (vehicle);
      const direction_t y_axis = unit_direction (Vec3 (0, 1, 0));
      uint64_t motion_part = motion_base + 0x100;
      const auto draw_part = [&] (const render::Mesh& mesh,
                                  const Mat4& transform) {
        r.draw_mesh (mesh, transform, ++motion_part);
      };
      // A unit flame cone stretched to a radius and a length.
      const auto cone_of = [] (meters_t radius, meters_t length) {
        return Mat4::scaling (Vec3 (
          meters_value (radius), meters_value (radius), meters_value (length)));
      };

      // Exhaust flame licking out of the muffler under load: an
      // additive two-layer lick with a pale core.
      if (exhaust) {
        const scale_factor_t lick =
          (0.85f +
           0.15f * std::sin (radians_value (
                     time * lick_fast +
                     std::sin (radians_value (time * lick_slow)) * u::rad))) *
          scale_factor[one];
        const Mat4 muffler =
          frame * Mat4::translation (bike_model::muffler_tip) *
          Mat4::rotation (180 * u::deg, direction_value (y_axis));
        draw_part (*fm.lick_outer,
                   muffler * cone_of (outer_radius,
                                      (0.16f * u::m + 0.30f * u::m * thrust) *
                                        lick.numerical_value_in (one)));
        draw_part (*fm.lick_core,
                   muffler * cone_of (core_radius,
                                      (0.22f * u::m + 0.36f * u::m * thrust) *
                                        lick.numerical_value_in (one)));
      }

      // Live jump-jet output, built fresh each frame from soft sprites that
      // face the camera: a white-hot throat, then overlapping blobs
      // stretched along the jet that cool from yellow through orange to a
      // fading red tail, with shock diamonds -- bright knots standing in
      // the exhaust -- pulsing down its first half and a heat glow round
      // each nozzle's mouth. Seen end-on, the stretched blobs round out, so
      // the jet never shows an edge.
      if (boosting)
        render_jets (r, vehicle, frame, camera, time, motion_part);
    }
  }
}
