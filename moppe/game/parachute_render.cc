#include <moppe/game/parachute_render.hh>

#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/gfx/mat4.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    namespace parachute_render_parts {
      displacement_t local (meters_t x, meters_t y, meters_t z) {
        return displacement (
          Vec3 (meters_value (x), meters_value (y), meters_value (z)));
      }

      // Thin cord/line drawn as a 4-sided prism between two local points.
      void draw_line_cord (render::DrawList& dl,
                           const displacement_t& a,
                           const displacement_t& b,
                           meters_t thick) {
        const displacement_t axis = b - a;
        if (axis.magnitude () < 1e-4f * u::m)
          return;
        const direction_t dir = direction_of (axis);
        const Vec3 raw_dir = direction_value (dir);
        Vec3 raw_u = cross (raw_dir, Vec3 (1.0f, 0.0f, 0.0f));
        if (length2 (raw_u) < 1e-5f)
          raw_u = cross (raw_dir, Vec3 (0.0f, 0.0f, 1.0f));
        const direction_t u = unit_direction (normalized (raw_u));
        const direction_t v =
          unit_direction (cross (raw_dir, direction_value (u)));
        const direction_t faces[4] = { u, v, -u, -v };
        for (int f = 0; f < 4; ++f) {
          const direction_t next = faces[(f + 1) % 4];
          const direction_t n = unit_direction (
            normalized (direction_value (faces[f]) + direction_value (next)));
          const displacement_t o0 = along (faces[f], thick);
          const displacement_t o1 = along (next, thick);
          dl.normal (n);
          dl.vertex (a + o0);
          dl.vertex (b + o0);
          dl.vertex (b + o1);
          dl.vertex (a + o0);
          dl.vertex (b + o1);
          dl.vertex (a + o1);
        }
      }

      // Frame anchored at the rider's feet, tilted by the canopy's pendulum
      // sway.
      Mat4 parachute_frame (const ParachutePose& chute) {
        const direction_t up = unit_direction (Vec3 (0, 1, 0));
        Vec3 level = direction_value (chute.heading);
        level[1] = 0.0f;
        const direction_t fwd = unit_direction (
          length2 (level) > 1e-6f ? normalized (level) : Vec3 (0, 0, 1));
        const direction_t right = unit_direction (
          normalized (cross (direction_value (up), direction_value (fwd))));
        return Mat4::translation (position_value (chute.position)) *
               Mat4::basis (direction_value (right),
                            direction_value (up),
                            direction_value (fwd)) *
               Mat4::translation (Vec3 (0.0f, 1.35f, 0.0f)) *
               Mat4::rotation (chute.sway_roll, Vec3 (0, 0, 1)) *
               Mat4::rotation (chute.sway_pitch, Vec3 (1, 0, 0)) *
               Mat4::translation (Vec3 (0.0f, -1.35f, 0.0f));
      }

      AvatarSkeleton pose_jumper (const Mat4& frame,
                                  const ParachutePose& chute,
                                  seconds_t time) {
        const direction_t forward =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 0, 1))));
        const direction_t up =
          unit_direction (normalized (frame.transform_vector (Vec3 (0, 1, 0))));
        const direction_t right = unit_direction (
          normalized (cross (direction_value (up), direction_value (forward))));

        const radians_t kick_phase = seconds_value (time) * 2.1f * u::rad;
        const proportion_t flare =
          (chute.flare ? 1.0f : 0.0f) * proportion[one];

        HoldPose hold;
        hold.facing = forward;
        hold.up = up;
        hold.pelvis =
          position (frame.transform_point (Vec3 (0.0f, 1.02f, 0.0f)));
        hold.lean = (0.08f - 0.10f * flare.numerical_value_in (one)) * u::rad;
        hold.gaze =
          normalized (direction_value (forward) - direction_value (up) * 0.22f);
        hold.head_up = direction_value (up);
        hold.sole = unit_direction (normalized (
          direction_value (forward) * 0.85f - direction_value (up) * 0.52f));
        hold.instep = unit_direction (normalized (
          direction_value (up) * 0.85f + direction_value (forward) * 0.52f));

        // Both hands reach up to the steering toggles on the risers; pulling a
        // turn dips that hand lower, and flaring pulls both hands down.
        for (int i = 0; i < 2; ++i) {
          const int sign = i == 0 ? -1 : 1;
          const meters_t pull =
            (std::max (0.0f, sign * chute.turn.numerical_value_in (one)) *
               0.22f +
             flare.numerical_value_in (one) * 0.26f) *
            u::m;
          const displacement_t hand =
            local (sign * 0.30f * u::m, 1.94f * u::m - pull, 0.06f * u::m);
          hold.wrist[i] =
            position (frame.transform_point (displacement_value (hand)));
          hold.elbow_pole[i] = direction_value (right) * (sign * 0.85f) +
                               direction_value (forward) * 0.4f;
          // Relaxed dangling legs with gentle breeze sway.
          const meters_t kick_y = sign * 0.03f * libm::sin (kick_phase) * u::m;
          const meters_t kick_z = sign * 0.07f * libm::sin (kick_phase) * u::m;
          const displacement_t foot = local (
            sign * 0.14f * u::m, 0.24f * u::m - kick_y, -0.14f * u::m - kick_z);
          hold.ankle[i] =
            position (frame.transform_point (displacement_value (foot)));
          hold.knee_pole[i] = direction_value (forward) * 0.9f +
                              direction_value (right) * (sign * 0.2f);
        }
        return pose_holding (hold);
      }
    }
  }

  void render_parachute (render::DrawList& dl,
                         const ParachutePose& parachute,
                         seconds_t time,
                         Outfit outfit,
                         bool show_rider) {
    namespace pr = parachute_render_parts;
    const Mat4 frame = pr::parachute_frame (parachute);
    const proportion_t open = std::clamp (
      parachute.opening, 0.05f * proportion[one], 1.0f * proportion[one]);
    const proportion_t rem = 1.0f * proportion[one] - open;
    const proportion_t bloom =
      (1.0f - rem.numerical_value_in (one) * rem.numerical_value_in (one) *
                rem.numerical_value_in (one)) *
      proportion[one];
    const scale_factor_t radius_scale =
      (0.18f + 0.82f * bloom.numerical_value_in (one)) * scale_factor[one];
    const proportion_t flare =
      (parachute.flare ? 1.0f : 0.0f) * proportion[one];

    constexpr int gores = 12;
    constexpr int rings = 6;
    const meters_t max_radius =
      2.15f * u::m * radius_scale.numerical_value_in (one);
    const meters_t skirt_y =
      3.55f * u::m + (1.0f - bloom.numerical_value_in (one)) * 0.55f * u::m;
    const meters_t dome_height =
      1.32f * u::m * (0.65f + 0.35f * bloom.numerical_value_in (one));
    const meters_t vent_radius =
      0.24f * u::m * radius_scale.numerical_value_in (one);

    const DisplayColor coral_red (0.86f, 0.22f, 0.15f);
    const DisplayColor cream (0.96f, 0.92f, 0.82f);
    const DisplayColor rim_navy (0.14f, 0.20f, 0.34f);
    const DisplayColor line_color (0.86f, 0.84f, 0.78f);
    const DisplayColor harness_color (0.16f, 0.17f, 0.19f);
    const DisplayColor toggle_yellow (0.96f, 0.76f, 0.12f);

    dl.push ();
    dl.mult (frame);
    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);

    // 1. Ripstop fabric dome canopy (12 alternating coral-red and cream gores).
    dl.material (0.0f * proportion[one],
                 0.72f * proportion[one],
                 0.0f * proportion[one],
                 0.45f * proportion[one]);
    dl.begin (render::Prim::Triangles);

    const auto canopy_point = [&] (int g_idx, proportion_t sub, int r_idx) {
      const radians_t a =
        (PI2 * (static_cast<float> (g_idx) + sub.numerical_value_in (one)) /
         gores) *
        u::rad;
      const proportion_t u =
        (static_cast<float> (r_idx) / rings) * proportion[one];
      const radians_t theta =
        (u.numerical_value_in (one) * PI * 0.47f) * u::rad;
      const scale_factor_t puff =
        (1.0f +
         0.065f *
           libm::sin (radians_t (sub.numerical_value_in (one) * PI * u::rad)) *
           (1.0f - 0.7f * u.numerical_value_in (one)) *
           bloom.numerical_value_in (one)) *
        scale_factor[one];
      const radians_t wave =
        (seconds_value (time) * 4.2f + static_cast<float> (g_idx) * 0.8f) *
        u::rad;
      const scale_factor_t breath =
        (1.0f +
         0.02f * libm::sin (wave) * (1.0f - u.numerical_value_in (one))) *
        scale_factor[one];
      const meters_t rad =
        (vent_radius + (max_radius - vent_radius) * libm::cos (theta)) *
        puff.numerical_value_in (one) * breath.numerical_value_in (one);
      const meters_t brake_dip =
        (1.0f - u.numerical_value_in (one)) *
        (flare.numerical_value_in (one) * 0.22f *
           std::max (0.0f, -libm::cos (a)) +
         0.14f * parachute.turn.numerical_value_in (one) * libm::sin (a)) *
        u::m;
      const meters_t y = skirt_y + dome_height * libm::sin (theta) - brake_dip;
      return pr::local (rad * libm::sin (a), y, rad * libm::cos (a));
    };

    const auto canopy_normal = [&] (int g_idx, proportion_t sub, int r_idx) {
      const radians_t a =
        (PI2 * (static_cast<float> (g_idx) + sub.numerical_value_in (one)) /
         gores) *
        u::rad;
      const proportion_t u =
        (static_cast<float> (r_idx) / rings) * proportion[one];
      const radians_t theta =
        (u.numerical_value_in (one) * PI * 0.47f) * u::rad;
      return unit_direction (
        normalized (Vec3 (libm::cos (theta) * libm::sin (a),
                          libm::sin (theta) + 0.25f,
                          libm::cos (theta) * libm::cos (a))));
    };

    for (int g = 0; g < gores; ++g) {
      const DisplayColor gore_col = (g % 2 == 0) ? coral_red : cream;
      constexpr int across = 2;
      for (int r = 0; r < rings; ++r) {
        dl.color (r == 0 ? rim_navy : gore_col);
        for (int s = 0; s < across; ++s) {
          const proportion_t s0 =
            (static_cast<float> (s) / across) * proportion[one];
          const proportion_t s1 =
            (static_cast<float> (s + 1) / across) * proportion[one];
          const displacement_t p00 = canopy_point (g, s0, r);
          const displacement_t p10 = canopy_point (g, s1, r);
          const displacement_t p01 = canopy_point (g, s0, r + 1);
          const displacement_t p11 = canopy_point (g, s1, r + 1);
          const direction_t n00 = canopy_normal (g, s0, r);
          const direction_t n10 = canopy_normal (g, s1, r);
          const direction_t n01 = canopy_normal (g, s0, r + 1);
          const direction_t n11 = canopy_normal (g, s1, r + 1);
          dl.normal (n00);
          dl.vertex (p00);
          dl.normal (n10);
          dl.vertex (p10);
          dl.normal (n11);
          dl.vertex (p11);
          dl.normal (n00);
          dl.vertex (p00);
          dl.normal (n11);
          dl.vertex (p11);
          dl.normal (n01);
          dl.vertex (p01);
        }
      }
    }
    dl.end ();

    // 2. 12 taut suspension lines from skirt ribs down to left/right risers.
    const displacement_t riser_left =
      pr::local (-0.28f * u::m, 2.04f * u::m, 0.02f * u::m);
    const displacement_t riser_right =
      pr::local (0.28f * u::m, 2.04f * u::m, 0.02f * u::m);
    dl.material (0.0f * proportion[one],
                 0.85f * proportion[one],
                 0.0f * proportion[one],
                 0.0f * proportion[one]);
    dl.color (line_color);
    dl.begin (render::Prim::Triangles);
    for (int g = 0; g < gores; ++g) {
      const displacement_t skirt = canopy_point (g, 0.0f * proportion[one], 0);
      const displacement_t& riser =
        displacement_value (skirt)[0] < 0.0f ? riser_left : riser_right;
      pr::draw_line_cord (dl, riser, skirt, 0.008f * u::m);
    }
    dl.end ();

    // 3. Harness webbing straps from shoulders up to risers.
    dl.color (harness_color);
    dl.begin (render::Prim::Triangles);
    pr::draw_line_cord (dl,
                        pr::local (-0.18f * u::m, 1.50f * u::m, 0.0f * u::m),
                        riser_left,
                        0.018f * u::m);
    pr::draw_line_cord (dl,
                        pr::local (0.18f * u::m, 1.50f * u::m, 0.0f * u::m),
                        riser_right,
                        0.018f * u::m);
    dl.end ();

    // 4. Steering toggles in the jumper's hands.
    dl.color (toggle_yellow);
    dl.begin (render::Prim::Triangles);
    for (int i = 0; i < 2; ++i) {
      const int sign = i == 0 ? -1 : 1;
      const meters_t pull =
        (std::max (0.0f, sign * parachute.turn.numerical_value_in (one)) *
           0.22f +
         flare.numerical_value_in (one) * 0.26f) *
        u::m;
      const displacement_t hand =
        pr::local (sign * 0.30f * u::m, 1.94f * u::m - pull, 0.06f * u::m);
      const displacement_t& riser = i == 0 ? riser_left : riser_right;
      pr::draw_line_cord (dl, hand, riser, 0.009f * u::m);
      pr::draw_line_cord (
        dl,
        hand + pr::local (-0.04f * u::m, 0.0f * u::m, 0.0f * u::m),
        hand + pr::local (0.04f * u::m, 0.0f * u::m, 0.0f * u::m),
        0.016f * u::m);
    }
    dl.end ();

    dl.plain ();
    dl.state (render::DrawState ());
    dl.pop ();

    if (show_rider)
      figure::draw (dl, pr::pose_jumper (frame, parachute, time), outfit);
  }
}
