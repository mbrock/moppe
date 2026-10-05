#include <moppe/game/walker_render.hh>

#include <moppe/game/avatar.hh>
#include <moppe/game/model.hh>

#include <cmath>

namespace moppe::game {
  namespace avatar_mesh {
    // Emits one flat-shaded triangle wound counter-clockwise about the
    // normal that faces away from `inside`.
    void facet (render::DrawList& dl,
                const Vec3& a,
                const Vec3& b,
                const Vec3& c,
                const Vec3& inside) {
      Vec3 n = cross (b - a, c - a);
      if (length2 (n) < 1e-12f)
        return;
      normalize (n);
      dl.normal (dot (n, a - inside) >= 0.0f ? n : -n);
      dl.vertex (a);
      if (dot (n, a - inside) >= 0.0f) {
        dl.vertex (b);
        dl.vertex (c);
      } else {
        dl.vertex (c);
        dl.vertex (b);
      }
    }

    // A faceted, tapered prism from `a` to `b`. Its cross-section is an
    // ellipse sampled at `sides` corners, `width` along the part of
    // `across` square to the axis and `depth` square to both.
    void limb (render::DrawList& dl,
               const Vec3& a,
               const Vec3& b,
               const Vec3& across,
               float width_a,
               float depth_a,
               float width_b,
               float depth_b,
               int sides = 6,
               float turn = 0.0f) {
      const Vec3 axis_span = b - a;
      if (length2 (axis_span) < 1e-10f)
        return;
      const Vec3 axis = normalized (axis_span);
      Vec3 u = across - axis * dot (across, axis);
      if (length2 (u) < 1e-8f)
        u = cross (axis, Vec3 (0, 0, 1));
      normalize (u);
      const Vec3 v = cross (axis, u);
      constexpr int most = 12;
      Vec3 ring_a[most], ring_b[most];
      sides = std::min (sides, most);
      for (int i = 0; i < sides; ++i) {
        const float angle = turn + PI2 * static_cast<float> (i) / sides;
        const float c = std::cos (angle), s = std::sin (angle);
        ring_a[i] = a + u * (width_a * c) + v * (depth_a * s);
        ring_b[i] = b + u * (width_b * c) + v * (depth_b * s);
      }
      const Vec3 middle = (a + b) * 0.5f;
      dl.begin (render::Prim::Triangles);
      for (int i = 0; i < sides; ++i) {
        const int j = (i + 1) % sides;
        facet (dl, ring_a[i], ring_a[j], ring_b[j], middle);
        facet (dl, ring_a[i], ring_b[j], ring_b[i], middle);
      }
      for (int i = 1; i + 1 < sides; ++i) {
        facet (dl, ring_a[0], ring_a[i], ring_a[i + 1], middle);
        facet (dl, ring_b[0], ring_b[i], ring_b[i + 1], middle);
      }
      dl.end ();
    }

    // A box in the frame (across, along, deep), centred at `centre` with
    // the given half extents.
    void block (render::DrawList& dl,
                const Vec3& centre,
                const Vec3& across,
                const Vec3& along,
                float half_across,
                float half_along,
                float half_deep) {
      constexpr float corner = 1.41421356f;
      limb (dl,
            centre - along * half_along,
            centre + along * half_along,
            across,
            half_across * corner,
            half_deep * corner,
            half_across * corner,
            half_deep * corner,
            4,
            PI / 4.0f);
    }

    // A low-poly ellipsoid: a few rings of flat facets, the stylised
    // heads and hands of the figure.
    void gem (render::DrawList& dl,
              const Vec3& centre,
              const Vec3& across,
              const Vec3& up,
              float rx,
              float ry,
              float rz,
              int slices = 7,
              int stacks = 5) {
      const Vec3 x = normalized (across - up * dot (across, up));
      const Vec3 y = normalized (up);
      const Vec3 z = cross (x, y);
      const auto point = [&] (int stack, int slice) {
        const float lat = PI * static_cast<float> (stack) / stacks - PI / 2;
        const float lon = PI2 * static_cast<float> (slice) / slices;
        return centre + x * (rx * std::cos (lat) * std::cos (lon)) +
               y * (ry * std::sin (lat)) +
               z * (rz * std::cos (lat) * std::sin (lon));
      };
      dl.begin (render::Prim::Triangles);
      for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
          const Vec3 p00 = point (i, j), p01 = point (i, j + 1);
          const Vec3 p10 = point (i + 1, j), p11 = point (i + 1, j + 1);
          if (i > 0)
            facet (dl, p00, p01, p11, centre);
          if (i + 1 < stacks)
            facet (dl, p00, p11, p10, centre);
        }
      dl.end ();
    }
  }

  void
  render_walker (render::DrawList& draw, const WalkerPose& walker, float time) {
    using namespace avatar_mesh;
    using model::RiderMaterial;
    const AvatarSkeleton k = pose_avatar (walker, time);
    draw.set_texture (nullptr);

    const Vec3 chest_right = normalized (cross (k.chest_up, k.chest_forward));
    const DisplayColor skin (0.80f, 0.58f, 0.44f);
    const DisplayColor beanie (0.62f, 0.20f, 0.12f);
    const DisplayColor pack (0.36f, 0.38f, 0.26f);
    const DisplayColor strap (0.18f, 0.17f, 0.15f);

    // Legs: boot, shin, knee, thigh. The boot is a block from heel to toe
    // around the ankle; its shaft climbs a third of the shin.
    for (int i = 0; i < 2; ++i) {
      const Vec3 sole = normalized (k.toe[i] - k.heel[i]);
      const Vec3 mid = (k.heel[i] + k.toe[i]) * 0.5f;
      Vec3 instep = k.ankle[i] - mid;
      instep = normalized (instep - sole * dot (instep, sole));
      const Vec3 foot_across = cross (instep, sole);
      model::rider_material (draw, RiderMaterial::Boots);
      block (
        draw, mid + instep * 0.045f, foot_across, sole, 0.052f, 0.135f, 0.048f);
      const Vec3 shaft = k.ankle[i] + (k.knee[i] - k.ankle[i]) * 0.33f;
      limb (draw,
            k.ankle[i] - instep * 0.02f,
            shaft,
            k.right,
            0.062f,
            0.068f,
            0.066f,
            0.07f);

      model::rider_material (draw, RiderMaterial::Pants);
      limb (draw, shaft, k.knee[i], k.right, 0.058f, 0.062f, 0.066f, 0.068f);
      gem (draw,
           k.knee[i],
           k.right,
           k.knee[i] - k.ankle[i],
           0.068f,
           0.07f,
           0.07f,
           6,
           4);
      limb (
        draw, k.knee[i], k.hip[i], k.right, 0.068f, 0.072f, 0.09f, 0.092f, 7);
    }

    // Hips and torso: a pelvis block, then a jersey that widens to the
    // shoulders and narrows to the collar.
    model::rider_material (draw, RiderMaterial::Pants);
    block (draw, k.pelvis + k.up * 0.03f, k.right, k.up, 0.17f, 0.09f, 0.11f);
    model::rider_material (draw, RiderMaterial::Jersey);
    limb (draw,
          k.pelvis + k.up * 0.06f,
          k.chest,
          chest_right,
          0.155f,
          0.105f,
          0.20f,
          0.125f,
          8);
    limb (draw, k.chest, k.neck, chest_right, 0.20f, 0.125f, 0.13f, 0.09f, 8);

    // A hiker's pack rides high on the back, with dark straps over the
    // shoulders.
    const Vec3 pack_centre =
      k.chest - k.chest_forward * 0.20f + k.chest_up * 0.03f;
    draw.color (pack);
    block (draw, pack_centre, chest_right, k.chest_up, 0.15f, 0.22f, 0.085f);
    block (draw,
           pack_centre - k.chest_forward * 0.09f - k.chest_up * 0.06f,
           chest_right,
           k.chest_up,
           0.11f,
           0.10f,
           0.04f);
    draw.color (beanie);
    block (draw,
           pack_centre + k.chest_up * 0.25f,
           chest_right,
           k.chest_forward,
           0.14f,
           0.07f,
           0.045f);
    draw.color (strap);
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      const Vec3 top =
        k.neck + chest_right * (side * 0.10f) + k.chest_up * 0.015f;
      limb (draw,
            top - k.chest_forward * 0.12f,
            top + k.chest_forward * 0.03f,
            k.chest_up,
            0.025f,
            0.012f,
            0.025f,
            0.012f,
            4);
      limb (draw,
            top + k.chest_forward * 0.03f,
            k.chest + chest_right * (side * 0.12f) + k.chest_forward * 0.12f,
            chest_right,
            0.025f,
            0.012f,
            0.025f,
            0.012f,
            4);
    }

    // Arms: shoulder, sleeve, forearm, glove.
    for (int i = 0; i < 2; ++i) {
      model::rider_material (draw, RiderMaterial::Jersey);
      gem (draw,
           k.shoulder[i],
           k.chest_forward,
           k.chest_up,
           0.07f,
           0.065f,
           0.07f,
           6,
           4);
      limb (draw,
            k.shoulder[i],
            k.elbow[i],
            k.chest_forward,
            0.058f,
            0.058f,
            0.048f,
            0.05f);
      limb (draw,
            k.elbow[i],
            k.wrist[i],
            k.chest_forward,
            0.046f,
            0.046f,
            0.037f,
            0.04f);
      model::rider_material (draw, RiderMaterial::Gloves);
      const Vec3 hand = normalized (k.wrist[i] - k.elbow[i]);
      gem (draw,
           k.wrist[i] + hand * 0.05f,
           chest_right,
           hand,
           0.035f,
           0.06f,
           0.045f,
           6,
           4);
    }

    // Neck, head, and a knitted cap pulled low.
    draw.color (skin);
    limb (draw,
          k.neck - k.chest_up * 0.02f,
          k.head - k.head_up * 0.06f,
          k.right,
          0.05f,
          0.05f,
          0.048f,
          0.048f);
    gem (draw, k.head, k.right, k.head_up, 0.092f, 0.115f, 0.105f, 8, 5);
    draw.color (beanie);
    gem (draw,
         k.head + k.head_up * 0.045f - k.head_forward * 0.012f,
         k.right,
         k.head_up,
         0.102f,
         0.088f,
         0.112f,
         8,
         4);
    // A dark band of brow and nose so the face reads at a distance.
    draw.color (DisplayColor (0.55f, 0.36f, 0.27f));
    block (draw,
           k.head + k.head_forward * 0.098f - k.head_up * 0.015f,
           k.right,
           k.head_up,
           0.016f,
           0.035f,
           0.016f);
  }
}
