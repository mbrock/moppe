#include <moppe/game/walker_render.hh>

#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/fishing.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <array>
#include <cmath>

namespace moppe::game {
  namespace {
    constexpr radians_t basket_tau = 6.2831853f * u::rad;
    // The basket's rim, half its length and width, the depth of its
    // body, and the height of the handle's arch over the rim.
    constexpr meters_t rim_length = 0.19f * u::m;
    constexpr meters_t rim_width = 0.13f * u::m;
    constexpr meters_t basket_depth = 0.13f * u::m;
    constexpr meters_t handle_rise = 0.20f * u::m;
    // How many of the gathered mushrooms show in it.
    constexpr std::size_t basket_shown = 18;

    // An offset as what moves a place (position_t is rooted in
    // isq::position_vector).
    auto shift (const displacement_t& offset) {
      return quantity_cast<isq::position_vector> (offset);
    }

    struct BasketFrame {
      position_t rim;      // centre of the rim
      direction_t forward; // along its length
      direction_t side;    // across it
      direction_t up;
    };

    // The basket hangs from the left hand, its handle in the fingers, a
    // little out from the thigh so the legs swing past it.
    BasketFrame basket_frame (const AvatarSkeleton& k) {
      const direction_t up = unit_direction (Vec3 (0, 1, 0));
      Vec3 level = direction_value (k.forward);
      level[1] = 0.0f;
      const direction_t forward = unit_direction (
        length2 (level) > 1e-6f ? normalized (level) : Vec3 (0, 0, 1));
      const direction_t side = unit_direction (
        cross (direction_value (up), direction_value (forward)));
      position_t hand = k.wrist[0];
      const displacement_t forearm = displacement (position_value (k.wrist[0]) -
                                                   position_value (k.elbow[0]));
      if (length2 (displacement_value (forearm)) > 1e-6f)
        hand += shift (along (direction_of (forearm), 0.07f * u::m));
      // Out from the body: the left hand's side.
      const displacement_t out = along (k.right, -0.09f * u::m);
      return { .rim = hand + shift (out) - shift (along (up, handle_rise)),
               .forward = forward,
               .side = side,
               .up = up };
    }

    // A point on the wicker's surface: `around` the axis, `down` from the
    // rim (0) to the floor (1).
    position_t
    wicker_point (const BasketFrame& b, radians_t around, proportion_t down) {
      // The share of the rim's size the wicker keeps that far down.
      const proportion_t taper = 1.0f - 0.22f * down;
      const float a = radians_value (around);
      return b.rim +
             shift (along (b.forward, rim_length * taper * std::cos (a))) +
             shift (along (b.side, rim_width * taper * std::sin (a))) -
             shift (along (b.up, basket_depth * down));
    }

    direction_t wicker_normal (const BasketFrame& b, radians_t around) {
      const float a = radians_value (around);
      const auto bulge = b.forward * (std::cos (a) / rim_length) +
                         b.side * (std::sin (a) / rim_width) +
                         b.up * (-1.5f / u::m);
      return unit_direction (
        normalized (bulge.numerical_value_in (one / u::m)));
    }

    void draw_wicker (render::DrawList& dl, const BasketFrame& b) {
      constexpr int sides = 18;
      constexpr int bands = 6;
      // Woven willow: bands of light and darker withies.
      const DisplayColor light (0.70f, 0.52f, 0.30f);
      const DisplayColor dark (0.52f, 0.36f, 0.19f);
      for (int band = 0; band < bands; ++band) {
        const proportion_t d0 =
          static_cast<float> (band) / bands * proportion[one];
        const proportion_t d1 =
          static_cast<float> (band + 1) / bands * proportion[one];
        for (int s = 0; s < sides; ++s) {
          const radians_t a0 = basket_tau * s / sides;
          const radians_t a1 = basket_tau * (s + 1) / sides;
          // A checker of over and under.
          dl.color ((band + s) % 2 ? light : dark);
          const auto put = [&] (radians_t a, proportion_t d) {
            dl.normal (wicker_normal (b, a));
            dl.vertex (wicker_point (b, a, d));
          };
          put (a0, d0);
          put (a0, d1);
          put (a1, d1);
          put (a0, d0);
          put (a1, d1);
          put (a1, d0);
        }
      }
      // The floor.
      const proportion_t floor = 1.0f * proportion[one];
      dl.color (dark);
      dl.normal (b.up);
      const position_t middle = b.rim - shift (along (b.up, basket_depth));
      for (int s = 0; s < sides; ++s) {
        dl.vertex (middle);
        dl.vertex (wicker_point (b, basket_tau * s / sides, floor));
        dl.vertex (wicker_point (b, basket_tau * (s + 1) / sides, floor));
      }
      // A thick rolled rim and the handle's arch, both as tubes, `centre`
      // giving the spine at a share of the way along.
      const auto tube = [&] (auto centre, int segments, meters_t radius) {
        constexpr int round = 6;
        for (int i = 0; i < segments; ++i) {
          const position_t p0 =
            centre (static_cast<float> (i) / segments * proportion[one]);
          const position_t p1 =
            centre (static_cast<float> (i + 1) / segments * proportion[one]);
          const direction_t axis = direction_of (p1 - p0);
          Vec3 u = cross (direction_value (axis), direction_value (b.up));
          if (length2 (u) < 1e-6f)
            u = cross (direction_value (axis), direction_value (b.side));
          u = normalized (u);
          const Vec3 v = cross (direction_value (axis), u);
          for (int r = 0; r < round; ++r) {
            const float t0 = radians_value (basket_tau * r / round);
            const float t1 = radians_value (basket_tau * (r + 1) / round);
            const direction_t n0 =
              unit_direction (u * std::cos (t0) + v * std::sin (t0));
            const direction_t n1 =
              unit_direction (u * std::cos (t1) + v * std::sin (t1));
            const auto put = [&] (const position_t& p, const direction_t& n) {
              dl.normal (n);
              dl.vertex (p + shift (along (n, radius)));
            };
            put (p0, n0);
            put (p1, n0);
            put (p1, n1);
            put (p0, n0);
            put (p1, n1);
            put (p0, n1);
          }
        }
      };
      dl.color (light);
      tube (
        [&] (proportion_t t) {
          return wicker_point (b, basket_tau * t, 0.0f * proportion[one]);
        },
        sides,
        0.012f * u::m);
      tube (
        [&] (proportion_t t) {
          const float a = radians_value (PI * u::rad * t);
          return b.rim +
                 shift (along (b.forward, 0.96f * rim_length * std::cos (a))) +
                 shift (along (b.up, handle_rise * std::sin (a)));
        },
        10,
        0.011f * u::m);
    }

    // The gathered mushrooms lie heaped in it, the latest on top.
    void draw_contents (render::DrawList& dl,
                        const BasketFrame& b,
                        std::span<const MushroomSite> basket) {
      const std::size_t first =
        basket.size () > basket_shown ? basket.size () - basket_shown : 0;
      for (std::size_t i = first; i < basket.size (); ++i) {
        const std::size_t place = i - first;
        const std::uint32_t seed = basket[i].seed;
        // A ring of places around the floor, then another a layer up.
        const std::size_t layer = place / 7;
        const radians_t around =
          basket_tau * (static_cast<float> (place % 7) / 7 + 0.5f * layer / 3 +
                        0.08f * hash_lane (seed, 21));
        const proportion_t out =
          (place % 7 == 6 ? 0.0f : 0.55f) * proportion[one];
        const proportion_t depth =
          std::max (0.25f, 0.85f - 0.3f * layer) * proportion[one];
        const float a = radians_value (around);
        MushroomSite lying = basket[i];
        lying.base =
          b.rim + shift (along (b.forward, out * rim_length * std::cos (a))) +
          shift (along (b.side, out * rim_width * std::sin (a))) -
          shift (along (b.up, basket_depth * depth));
        lying.scale = 0.5f * basket[i].scale;
        lying.lean = (0.5f + 0.8f * hash_lane (seed, 22)) * inclination[u::rad];
        lying.lean_toward =
          radians_value (basket_tau * hash_lane (seed, 23)) * heading[u::rad];
        draw_mushroom (dl, lying);
      }
    }

    // A hemp rope from the right hand to where it is tied, sagging a
    // little: a thin tube of four sides.
    void draw_rope (render::DrawList& dl,
                    const position_t& from,
                    const position_t& to) {
      constexpr int pieces = 12;
      constexpr meters_t thick = 0.014f * u::m;
      const displacement_t line = to - from;
      const meters_t span = length (displacement_value (line)) * u::m;
      if (span < 1e-3f * u::m)
        return;
      const Vec3 axis =
        displacement_value (line) * (1.0f / meters_value (span));
      const Vec3 across = cross (axis, Vec3 (0, 1, 0));
      const direction_t side = unit_direction (
        length2 (across) > 1e-6f ? normalized (across) : Vec3 (1, 0, 0));
      const direction_t over =
        unit_direction (cross (direction_value (side), axis));
      const direction_t up = unit_direction (Vec3 (0, 1, 0));
      const meters_t sag = 0.04f * span;
      const auto at = [&] (int piece) {
        const proportion_t t =
          static_cast<float> (piece) / pieces * proportion[one];
        return from + shift (line * t) -
               shift (along (up, sag * 4.0f * t * (1.0f - t)));
      };
      render::DrawState solid;
      solid.cull = false;
      dl.state (solid);
      dl.lit (true);
      dl.fogged (true);
      dl.begin (render::Prim::Triangles);
      dl.color (DisplayColor (0.60f, 0.50f, 0.32f));
      const direction_t faces[4] = { side, over, -side, -over };
      for (int piece = 0; piece < pieces; ++piece) {
        const position_t a = at (piece), b = at (piece + 1);
        for (int f = 0; f < 4; ++f) {
          const displacement_t u = along (faces[f], thick),
                               v = along (faces[(f + 1) % 4], thick);
          dl.normal (
            unit_direction (normalized (direction_value (faces[f]) +
                                        direction_value (faces[(f + 1) % 4]))));
          dl.vertex (a + shift (u));
          dl.vertex (b + shift (u));
          dl.vertex (b + shift (v));
          dl.vertex (a + shift (u));
          dl.vertex (b + shift (v));
          dl.vertex (a + shift (v));
        }
      }
      dl.end ();
      dl.state (render::DrawState ());
    }

    void draw_basket (render::DrawList& dl,
                      const BasketFrame& b,
                      std::span<const MushroomSite> basket) {
      render::DrawState solid;
      solid.cull = false;
      dl.state (solid);
      dl.lit (true);
      dl.fogged (true);
      dl.begin (render::Prim::Triangles);
      draw_wicker (dl, b);
      draw_contents (dl, b, basket);
      dl.end ();
      dl.state (render::DrawState ());
    }
  }

  void render_walker (render::DrawList& draw,
                      const WalkerPose& walker,
                      seconds_t time,
                      std::span<const MushroomSite> basket,
                      Outfit outfit) {
    const AvatarSkeleton k = pose_avatar (walker, time);
    figure::draw (draw, k, outfit);
    draw_basket (draw, basket_frame (k), basket);
    if (walker.rope_end)
      draw_rope (draw,
                 k.wrist[1] +
                   shift (displacement (position_value (k.wrist[1]) -
                                        position_value (k.elbow[1])) *
                          0.25f),
                 *walker.rope_end);
    if (walker.rod)
      draw_rod (draw, k, *walker.rod, seconds_value (time));
  }

  void render_basket (render::DrawList& draw,
                      const WalkerPose& walker,
                      seconds_t time,
                      std::span<const MushroomSite> basket) {
    const AvatarSkeleton k = pose_avatar (walker, time);
    draw_basket (draw, basket_frame (k), basket);
    if (walker.rod)
      draw_rod (draw, k, *walker.rod, seconds_value (time));
  }

  void render_basket_set_down (render::DrawList& draw,
                               const position_t& floor,
                               const direction_t& forward,
                               std::span<const MushroomSite> basket) {
    const direction_t up = unit_direction (Vec3 (0, 1, 0));
    Vec3 level = direction_value (forward);
    level[1] = 0.0f;
    const direction_t lengthwise = unit_direction (
      length2 (level) > 1e-6f ? normalized (level) : Vec3 (0, 0, 1));
    draw_basket (draw,
                 { .rim = floor + shift (along (up, basket_depth)),
                   .forward = lengthwise,
                   .side = unit_direction (cross (
                     direction_value (up), direction_value (lengthwise))),
                   .up = up },
                 basket);
  }
}
