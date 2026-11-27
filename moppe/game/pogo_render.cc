#include <moppe/game/pogo_render.hh>

#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/mov/pogo.hh>

#include <cmath>

namespace moppe::game {
  namespace {
    namespace pogo_render_parts {
      // Draws a closed cylinder from `a` to `b` of radius `radius` into `dl`
      // (already inside `dl.begin(Prim::Triangles)`).
      void draw_cylinder (render::DrawList& dl,
                          const displacement_t& a,
                          const displacement_t& b,
                          meters_t radius,
                          int sides) {
        const displacement_t axis = b - a;
        const meters_t span = axis.magnitude ();
        if (span < 1e-5f * u::m)
          return;
        const direction_t dir = direction_of (axis);
        const Vec3 raw_dir = direction_value (dir);
        Vec3 raw_u = cross (raw_dir, Vec3 (0.0f, 1.0f, 0.0f));
        if (length2 (raw_u) < 1e-5f)
          raw_u = cross (raw_dir, Vec3 (1.0f, 0.0f, 0.0f));
        const direction_t u = unit_direction (normalized (raw_u));
        const direction_t v =
          unit_direction (cross (raw_dir, direction_value (u)));
        const radians_t step = (PI2 / static_cast<float> (sides)) * u::rad;
        for (int s = 0; s < sides; ++s) {
          const radians_t t0 = step * static_cast<float> (s);
          const radians_t t1 = step * static_cast<float> (s + 1);
          const direction_t n0 =
            unit_direction (direction_value (u) * libm::cos (t0) +
                            direction_value (v) * libm::sin (t0));
          const direction_t n1 =
            unit_direction (direction_value (u) * libm::cos (t1) +
                            direction_value (v) * libm::sin (t1));
          const displacement_t a0 = a + along (n0, radius);
          const displacement_t a1 = a + along (n1, radius);
          const displacement_t b0 = b + along (n0, radius);
          const displacement_t b1 = b + along (n1, radius);
          dl.normal (n0);
          dl.vertex (a0);
          dl.vertex (b0);
          dl.normal (n1);
          dl.vertex (b1);
          dl.normal (n0);
          dl.vertex (a0);
          dl.normal (n1);
          dl.vertex (b1);
          dl.vertex (a1);
          // End caps.
          dl.normal (-dir);
          dl.vertex (a);
          dl.vertex (a1);
          dl.vertex (a0);
          dl.normal (dir);
          dl.vertex (b);
          dl.vertex (b0);
          dl.vertex (b1);
        }
      }

      // Draws a 3D helical coil spring between y0 and y1 along local +Y.
      void draw_coil_spring (render::DrawList& dl,
                             meters_t y0,
                             meters_t y1,
                             meters_t coil_radius,
                             meters_t wire_radius,
                             int turns,
                             int steps_per_turn) {
        const int total = turns * steps_per_turn;
        constexpr int ring = 6;
        const meters_t span = y1 - y0;
        const auto spine = [&] (int i) {
          const proportion_t t =
            (static_cast<float> (i) / total) * proportion[one];
          const radians_t a = (PI2 * static_cast<float> (turns)) *
                              t.numerical_value_in (one) * u::rad;
          return displacement (
            Vec3 (meters_value (coil_radius) * libm::cos (a),
                  meters_value (y0 + span * t.numerical_value_in (one)),
                  meters_value (coil_radius) * libm::sin (a)));
        };
        const radians_t ring_step = (PI2 / static_cast<float> (ring)) * u::rad;
        for (int i = 0; i < total; ++i) {
          const displacement_t p0 = spine (i);
          const displacement_t p1 = spine (i + 1);
          const direction_t dir = direction_of (p1 - p0);
          const Vec3 raw_dir = direction_value (dir);
          const Vec3 raw_p0 = displacement_value (p0);
          const Vec3 radial = normalized (Vec3 (raw_p0[0], 0.0f, raw_p0[2]));
          const direction_t u = unit_direction (
            normalized (radial - raw_dir * dot (radial, raw_dir)));
          const direction_t v =
            unit_direction (cross (raw_dir, direction_value (u)));
          for (int s = 0; s < ring; ++s) {
            const radians_t a0 = ring_step * static_cast<float> (s);
            const radians_t a1 = ring_step * static_cast<float> (s + 1);
            const direction_t n0 =
              unit_direction (direction_value (u) * libm::cos (a0) +
                              direction_value (v) * libm::sin (a0));
            const direction_t n1 =
              unit_direction (direction_value (u) * libm::cos (a1) +
                              direction_value (v) * libm::sin (a1));
            dl.normal (n0);
            dl.vertex (p0 + along (n0, wire_radius));
            dl.vertex (p1 + along (n0, wire_radius));
            dl.normal (n1);
            dl.vertex (p1 + along (n1, wire_radius));
            dl.normal (n0);
            dl.vertex (p0 + along (n0, wire_radius));
            dl.normal (n1);
            dl.vertex (p1 + along (n1, wire_radius));
            dl.vertex (p0 + along (n1, wire_radius));
          }
        }
      }

      displacement_t local (meters_t x, meters_t y, meters_t z) {
        return displacement (
          Vec3 (meters_value (x), meters_value (y), meters_value (z)));
      }

      Mat4 pogo_frame (const PogoPose& pogo) {
        const direction_t up = unit_direction (Vec3 (0, 1, 0));
        Vec3 level = direction_value (pogo.heading);
        level[1] = 0.0f;
        const direction_t fwd = unit_direction (
          length2 (level) > 1e-6f ? normalized (level) : Vec3 (0, 0, 1));
        const direction_t right = unit_direction (
          normalized (cross (direction_value (up), direction_value (fwd))));
        return Mat4::translation (position_value (pogo.position)) *
               Mat4::basis (direction_value (right),
                            direction_value (up),
                            direction_value (fwd)) *
               Mat4::rotation (pogo.pitch, Vec3 (1, 0, 0)) *
               Mat4::rotation (pogo.roll, Vec3 (0, 0, 1));
      }

      AvatarSkeleton pose_pogo_rider (const Mat4& frame,
                                      const PogoPose& pogo,
                                      seconds_t time) {
        const direction_t forward =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 0, 1))));
        const direction_t up =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 1, 0))));
        const direction_t right = unit_direction (
          normalized (cross (direction_value (up), direction_value (forward))));

        const proportion_t comp = pogo.compression;
        const meters_t peg_y = 0.62f * u::m - comp.numerical_value_in (one) *
                                                mov::Pogo::spring_travel;
        const meters_t bar_y = peg_y + 1.00f * u::m;
        constexpr meters_t shaft_z = 0.07f * u::m;

        // Knees bend deeply as the coil spring compresses and extend joyfully
        // on launch; subtle breathing micro-sway in the air.
        const meters_t crouch =
          (0.10f + 0.24f * comp.numerical_value_in (one)) * u::m;
        const meters_t breath =
          0.008f *
          libm::sin (radians_t (seconds_value (time) * 3.0f * u::rad)) * u::m;
        const displacement_t pelvis_local = local (
          0.0f * u::m, peg_y + 0.90f * u::m - crouch + breath, -0.14f * u::m);

        HoldPose hold;
        hold.facing = forward;
        hold.up = up;
        hold.pelvis =
          position (frame.transform_point (displacement_value (pelvis_local)));
        hold.lean = (0.14f + 0.16f * comp.numerical_value_in (one)) * u::rad;
        hold.gaze = normalized (direction_value (forward) +
                                direction_value (up) *
                                  libm::sin (radians_t (pogo.look_pitch)));
        hold.head_up = direction_value (up);
        hold.sole = forward;
        hold.instep = up;

        for (int i = 0; i < 2; ++i) {
          const int sign = i == 0 ? -1 : 1;
          const displacement_t grip =
            local (sign * 0.23f * u::m, bar_y + 0.01f * u::m, shaft_z);
          const displacement_t peg =
            local (sign * 0.16f * u::m, peg_y + 0.08f * u::m, -0.02f * u::m);
          hold.wrist[i] =
            position (frame.transform_point (displacement_value (grip)));
          hold.elbow_pole[i] = direction_value (right) * (sign * 0.55f) -
                               direction_value (up) * 0.85f -
                               direction_value (forward) * 0.35f;
          hold.ankle[i] =
            position (frame.transform_point (displacement_value (peg)));
          hold.knee_pole[i] = direction_value (forward) * 0.9f +
                              direction_value (right) * (sign * 0.30f);
        }
        return pose_holding (hold);
      }
    }
  }

  void render_pogo (render::DrawList& dl,
                    const PogoPose& pogo,
                    seconds_t time,
                    Outfit outfit,
                    bool show_rider) {
    namespace pr = pogo_render_parts;
    const Mat4 frame = pr::pogo_frame (pogo);
    const proportion_t comp = pogo.compression;
    const meters_t peg_y =
      0.62f * u::m - comp.numerical_value_in (one) * mov::Pogo::spring_travel;
    const meters_t bar_y = peg_y + 1.00f * u::m;
    constexpr meters_t shaft_z = 0.07f * u::m;
    const scale_factor_t tip_squish =
      (1.0f - 0.25f * comp.numerical_value_in (one)) * scale_factor[one];
    const scale_factor_t tip_wide =
      (1.0f + 0.22f * comp.numerical_value_in (one)) * scale_factor[one];

    // Bright Candy Orange enamel (matches Trail 90!), glowing slightly warmer
    // as bounce charge builds up.
    const proportion_t charge = pogo.charge;
    const DisplayColor enamel (0.95f + 0.05f * charge.numerical_value_in (one),
                               0.34f + 0.20f * charge.numerical_value_in (one),
                               0.05f);
    const DisplayColor chrome (0.88f, 0.90f, 0.93f);
    const DisplayColor rubber (0.10f, 0.10f, 0.11f);
    const DisplayColor grip_color (0.90f, 0.16f, 0.10f);

    constexpr meters_t zero = 0.0f * u::m;

    dl.push ();
    dl.mult (frame);
    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);

    // 1. Chunky black rubber bounce tip (slightly squished at bottom dead
    // centre).
    dl.material (0.0f * proportion[one],
                 0.82f * proportion[one],
                 0.0f * proportion[one],
                 0.0f * proportion[one]);
    dl.color (rubber);
    dl.push ();
    dl.translate (pr::local (
      zero, 0.075f * u::m * tip_squish.numerical_value_in (one), shaft_z));
    dl.scale (tip_wide.numerical_value_in (one),
              tip_squish.numerical_value_in (one),
              tip_wide.numerical_value_in (one));
    dl.sphere (0.085f * u::m, 14, 10);
    dl.pop ();

    // 2. Chrome lower telescoping piston rod + big exposed helical coil spring.
    dl.material (0.95f * proportion[one],
                 0.16f * proportion[one],
                 0.40f * proportion[one],
                 0.0f * proportion[one]);
    dl.color (chrome);
    dl.push ();
    dl.translate (pr::local (zero, zero, shaft_z));
    dl.begin (render::Prim::Triangles);
    pr::draw_cylinder (dl,
                       pr::local (zero, 0.07f * u::m, zero),
                       pr::local (zero, peg_y + 0.12f * u::m, zero),
                       0.028f * u::m,
                       12);
    pr::draw_cylinder (dl,
                       pr::local (zero, 0.11f * u::m, zero),
                       pr::local (zero, 0.15f * u::m, zero),
                       0.078f * u::m,
                       14);
    pr::draw_cylinder (dl,
                       pr::local (zero, peg_y - 0.04f * u::m, zero),
                       pr::local (zero, peg_y + 0.02f * u::m, zero),
                       0.080f * u::m,
                       14);
    pr::draw_coil_spring (dl,
                          0.14f * u::m,
                          peg_y - 0.035f * u::m,
                          0.068f * u::m,
                          0.016f * u::m,
                          8,
                          12);
    pr::draw_cylinder (dl,
                       pr::local (zero, bar_y - 0.14f * u::m, zero),
                       pr::local (zero, bar_y + 0.02f * u::m, zero),
                       0.028f * u::m,
                       12);
    pr::draw_cylinder (dl,
                       pr::local (-0.20f * u::m, bar_y, zero),
                       pr::local (0.20f * u::m, bar_y, zero),
                       0.022f * u::m,
                       12);
    dl.end ();
    dl.pop ();

    // 3. Glossy Candy Orange outer housing tube + footpeg cross-bracket.
    dl.material (0.15f * proportion[one],
                 0.18f * proportion[one],
                 0.90f * proportion[one],
                 0.0f * proportion[one]);
    dl.color (enamel);
    dl.begin (render::Prim::Triangles);
    pr::draw_cylinder (dl,
                       pr::local (zero, peg_y + 0.01f * u::m, shaft_z),
                       pr::local (zero, bar_y - 0.11f * u::m, shaft_z),
                       0.048f * u::m,
                       14);
    // Angled footpeg bridge back to the feet.
    pr::draw_cylinder (dl,
                       pr::local (zero, peg_y, shaft_z),
                       pr::local (zero, peg_y, zero),
                       0.030f * u::m,
                       12);
    pr::draw_cylinder (dl,
                       pr::local (-0.09f * u::m, peg_y, zero),
                       pr::local (0.09f * u::m, peg_y, zero),
                       0.028f * u::m,
                       12);
    dl.end ();

    // 4. Ribbed rubber footpegs.
    dl.material (0.0f * proportion[one],
                 0.78f * proportion[one],
                 0.0f * proportion[one],
                 0.0f * proportion[one]);
    dl.color (rubber);
    dl.begin (render::Prim::Triangles);
    pr::draw_cylinder (dl,
                       pr::local (-0.24f * u::m, peg_y, zero),
                       pr::local (-0.08f * u::m, peg_y, zero),
                       0.028f * u::m,
                       12);
    pr::draw_cylinder (dl,
                       pr::local (0.08f * u::m, peg_y, zero),
                       pr::local (0.24f * u::m, peg_y, zero),
                       0.028f * u::m,
                       12);
    dl.end ();

    // 5. Textured red rubber handgrips at the T-bar ends.
    dl.material (0.0f * proportion[one],
                 0.65f * proportion[one],
                 0.15f * proportion[one],
                 0.0f * proportion[one]);
    dl.color (grip_color);
    dl.begin (render::Prim::Triangles);
    pr::draw_cylinder (dl,
                       pr::local (-0.32f * u::m, bar_y, shaft_z),
                       pr::local (-0.17f * u::m, bar_y, shaft_z),
                       0.028f * u::m,
                       12);
    pr::draw_cylinder (dl,
                       pr::local (0.17f * u::m, bar_y, shaft_z),
                       pr::local (0.32f * u::m, bar_y, shaft_z),
                       0.028f * u::m,
                       12);
    dl.end ();

    dl.plain ();
    dl.state (render::DrawState ());
    dl.pop ();

    if (show_rider)
      figure::draw (dl, pr::pose_pogo_rider (frame, pogo, time), outfit);
  }
}
