#include <moppe/game/walker_render.hh>

#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <array>
#include <cmath>

namespace moppe::game {
  namespace {
    constexpr float basket_tau = 6.2831853f;
    // The basket's rim, half its length and width, the depth of its
    // body, and the height of the handle's arch over the rim.
    constexpr float rim_length = 0.19f;
    constexpr float rim_width = 0.13f;
    constexpr float basket_depth = 0.13f;
    constexpr float handle_rise = 0.20f;
    // How many of the gathered mushrooms show in it.
    constexpr std::size_t basket_shown = 18;

    struct BasketFrame {
      Vec3 rim;     // centre of the rim
      Vec3 forward; // along its length
      Vec3 side;    // across it
      Vec3 up;
    };

    // The basket hangs from the left hand, its handle in the fingers, a
    // little out from the thigh so the legs swing past it.
    BasketFrame basket_frame (const AvatarSkeleton& k) {
      const Vec3 up (0, 1, 0);
      Vec3 forward = k.forward;
      forward[1] = 0.0f;
      forward = length2 (forward) > 1e-6f ? normalized (forward)
                                          : Vec3 (0, 0, 1);
      const Vec3 side = cross (up, forward);
      Vec3 hand = k.wrist[0];
      const Vec3 along = k.wrist[0] - k.elbow[0];
      if (length2 (along) > 1e-6f)
        hand += normalized (along) * 0.07f;
      // Out from the body: the left hand's side.
      const Vec3 out = k.right * -0.09f;
      return { .rim = hand + out - up * handle_rise,
               .forward = forward,
               .side = side,
               .up = up };
    }

    // A point on the wicker's surface: `around` in radians about the
    // axis, `down` from the rim (0) to the floor (1).
    Vec3 wicker_point (const BasketFrame& b, float around, float down) {
      const float taper = 1.0f - 0.22f * down;
      return b.rim + b.forward * (rim_length * taper * std::cos (around)) +
             b.side * (rim_width * taper * std::sin (around)) -
             b.up * (basket_depth * down);
    }

    Vec3 wicker_normal (const BasketFrame& b, float around) {
      return normalized (b.forward * (std::cos (around) / rim_length) +
                         b.side * (std::sin (around) / rim_width) +
                         b.up * -1.5f);
    }

    void draw_wicker (render::DrawList& dl, const BasketFrame& b) {
      constexpr int sides = 18;
      constexpr int bands = 6;
      // Woven willow: bands of light and darker withies.
      const DisplayColor light (0.70f, 0.52f, 0.30f);
      const DisplayColor dark (0.52f, 0.36f, 0.19f);
      for (int band = 0; band < bands; ++band) {
        const float d0 = static_cast<float> (band) / bands;
        const float d1 = static_cast<float> (band + 1) / bands;
        for (int s = 0; s < sides; ++s) {
          const float a0 = basket_tau * s / sides;
          const float a1 = basket_tau * (s + 1) / sides;
          // A checker of over and under.
          dl.color ((band + s) % 2 ? light : dark);
          const auto put = [&] (float a, float d) {
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
      dl.color (dark);
      dl.normal (b.up);
      const Vec3 middle = b.rim - b.up * basket_depth;
      for (int s = 0; s < sides; ++s) {
        dl.vertex (middle);
        dl.vertex (wicker_point (b, basket_tau * s / sides, 1.0f));
        dl.vertex (wicker_point (b, basket_tau * (s + 1) / sides, 1.0f));
      }
      // A thick rolled rim and the handle's arch, both as tubes.
      const auto tube = [&] (auto centre, int segments, float radius) {
        constexpr int round = 6;
        for (int i = 0; i < segments; ++i) {
          const Vec3 p0 = centre (static_cast<float> (i) / segments);
          const Vec3 p1 = centre (static_cast<float> (i + 1) / segments);
          const Vec3 axis = normalized (p1 - p0);
          Vec3 u = cross (axis, b.up);
          if (length2 (u) < 1e-6f)
            u = cross (axis, b.side);
          u = normalized (u);
          const Vec3 v = cross (axis, u);
          for (int r = 0; r < round; ++r) {
            const float t0 = basket_tau * r / round;
            const float t1 = basket_tau * (r + 1) / round;
            const Vec3 n0 = u * std::cos (t0) + v * std::sin (t0);
            const Vec3 n1 = u * std::cos (t1) + v * std::sin (t1);
            const auto put = [&] (const Vec3& p, const Vec3& n) {
              dl.normal (n);
              dl.vertex (p + n * radius);
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
      tube ([&] (float t) { return wicker_point (b, basket_tau * t, 0.0f); },
            sides,
            0.012f);
      tube (
        [&] (float t) {
          const float a = PI * t;
          return b.rim + b.forward * (0.96f * rim_length * std::cos (a)) +
                 b.up * (handle_rise * std::sin (a));
        },
        10,
        0.011f);
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
        const float layer = static_cast<float> (place / 7);
        const float around = basket_tau * (static_cast<float> (place % 7) / 7 +
                                           0.5f * layer / 3 +
                                           0.08f * hash_lane (seed, 21));
        const float out = place % 7 == 6 ? 0.0f : 0.55f;
        const float depth = std::max (0.25f, 0.85f - 0.3f * layer);
        MushroomSite lying = basket[i];
        lying.base = b.rim +
                     b.forward * (out * rim_length * std::cos (around)) +
                     b.side * (out * rim_width * std::sin (around)) -
                     b.up * (basket_depth * depth);
        lying.scale = 0.5f * basket[i].scale;
        lying.lean = 0.5f + 0.8f * hash_lane (seed, 22);
        lying.lean_toward = basket_tau * hash_lane (seed, 23);
        draw_mushroom (dl, lying);
      }
    }

    void draw_basket (render::DrawList& dl,
                      const AvatarSkeleton& k,
                      std::span<const MushroomSite> basket) {
      const BasketFrame b = basket_frame (k);
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
                      float time,
                      std::span<const MushroomSite> basket) {
    const AvatarSkeleton k = pose_avatar (walker, time);
    figure::draw (draw, k);
    draw_basket (draw, k, basket);
  }

  void render_basket (render::DrawList& draw,
                      const WalkerPose& walker,
                      float time,
                      std::span<const MushroomSite> basket) {
    draw_basket (draw, pose_avatar (walker, time), basket);
  }
}
