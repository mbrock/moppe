#include <moppe/game/figure.hh>

#include <vector>

#include <string_view>

namespace moppe::game::figure {
  namespace {
    Vec3 vec (const std::array<float, 3>& a) {
      return Vec3 (a[0], a[1], a[2]);
    }

    std::array<float, 3> arr (const Vec3& v) {
      return { v[0], v[1], v[2] };
    }

    // The frame at `origin` whose y runs along `axis` and whose z leans
    // toward `reference`, as the rig's bones are rolled.
    Frame
    frame (const position_t& origin, const Vec3& axis, const Vec3& reference) {
      Vec3 y = axis;
      if (length2 (y) < 1e-12f)
        y = Vec3 (0, 1, 0);
      normalize (y);
      Vec3 z = reference - y * dot (reference, y);
      if (length2 (z) < 1e-8f) {
        // The reference lies along the bone; any square direction will do.
        z = std::abs (y[1]) < 0.9f ? cross (y, Vec3 (0, 1, 0))
                                   : cross (y, Vec3 (1, 0, 0));
      }
      normalize (z);
      return {
        arr (position_value (origin)), arr (cross (y, z)), arr (y), arr (z)
      };
    }

    // The rigid motion carrying a rest frame onto a posed one.
    struct Motion {
      Vec3 x, y, z, offset;

      Vec3 turn (const Vec3& v) const {
        return x * v[0] + y * v[1] + z * v[2];
      }

      Vec3 apply (const Vec3& p) const {
        return turn (p) + offset;
      }
    };

    Motion motion (const Frame& from, const Frame& to) {
      // to_axes * from_axesᵀ, applied as columns.
      const Vec3 fx = vec (from.x), fy = vec (from.y), fz = vec (from.z);
      const Vec3 tx = vec (to.x), ty = vec (to.y), tz = vec (to.z);
      Motion m;
      m.x = tx * fx[0] + ty * fy[0] + tz * fz[0];
      m.y = tx * fx[1] + ty * fy[1] + tz * fz[1];
      m.z = tx * fx[2] + ty * fy[2] + tz * fz[2];
      const Vec3 o = vec (from.origin);
      m.offset = vec (to.origin) - (m.x * o[0] + m.y * o[1] + m.z * o[2]);
      return m;
    }
  }

  std::array<Frame, bone_count> pose (const AvatarSkeleton& k) {
    // The way from one joint to the next, in the rig's metres.
    const auto bone = [] (const position_t& from, const position_t& to) {
      return (to - from).numerical_value_in (si::metre);
    };
    std::array<Frame, bone_count> f;
    f[pelvis] =
      frame (k.pelvis, bone (k.pelvis, k.waist), direction_value (k.forward));
    f[spine] = frame (
      k.waist, bone (k.waist, k.chest), direction_value (k.chest_forward));
    f[chest] = frame (
      k.chest, bone (k.chest, k.neck), direction_value (k.chest_forward));
    f[head] = frame (
      k.neck, direction_value (k.head_up), direction_value (k.head_forward));
    const Vec3 chest_right =
      cross (direction_value (k.chest_up), direction_value (k.chest_forward));
    const Vec3& right = direction_value (k.right);
    for (int i = 0; i < 2; ++i) {
      f[thigh + i] = frame (k.hip[i], bone (k.hip[i], k.knee[i]), right);
      f[shin + i] = frame (k.knee[i], bone (k.knee[i], k.ankle[i]), right);
      // The foot runs heel to toe and is rolled to its instep.
      const Vec3& heel = position_value (k.heel[i]);
      const Vec3& toe = position_value (k.toe[i]);
      const Vec3 mid = (heel + toe) * 0.5f;
      f[foot + i] =
        frame (k.ankle[i], toe - heel, position_value (k.ankle[i]) - mid);
      f[upper_arm + i] =
        frame (k.shoulder[i], bone (k.shoulder[i], k.elbow[i]), chest_right);
      f[forearm + i] =
        frame (k.elbow[i], bone (k.elbow[i], k.wrist[i]), chest_right);
      f[hand + i] =
        frame (k.wrist[i], bone (k.elbow[i], k.wrist[i]), chest_right);
    }
    return f;
  }

  void skin (const AvatarSkeleton& k,
             std::vector<Vec3>& points,
             std::vector<Vec3>& normals) {
    const std::array<Frame, bone_count> posed = pose (k);
    std::array<Motion, bone_count> moves;
    for (int b = 0; b < bone_count; ++b)
      moves[b] = motion (rest[b], posed[b]);

    const Vec3 origin = position_value (k.pelvis);
    points.resize (vertices.size ());
    normals.resize (vertices.size ());
    for (std::size_t i = 0; i < vertices.size (); ++i) {
      const Vertex& v = vertices[i];
      const Vec3 p = vec (v.position);
      const Vec3 n = vec (v.normal);
      // Blend offsets from the pelvis rather than world positions, and
      // renormalise the weights, so rounding in the exported weights
      // cannot shift a vertex by a fraction of its distance from the
      // world origin.
      Vec3 point (0, 0, 0), normal (0, 0, 0);
      float total = 0.0f;
      for (int j = 0; j < 4; ++j) {
        const float w = v.weight[j];
        if (w <= 0.0f)
          continue;
        const Motion& m = moves[v.bone[j]];
        point += (m.apply (p) - origin) * w;
        normal += m.turn (n) * w;
        total += w;
      }
      points[i] = origin + point * (1.0f / total);
      normals[i] = length2 (normal) > 1e-12f ? normalized (normal) : n;
    }
  }

  namespace {
    // Each companion outfit repaints the clothes and leaves the rest --
    // skin, sandals, buttons, buckles -- as modelled.
    struct Repaint {
      std::string_view name;
      DisplayColor colour;
    };

    const std::array<std::array<Repaint, 10>, outfit_count - 1>
      repainted_outfits = { {
        // green_jacket: forest green jacket, orange cap, khaki trousers
        { {
          { "jacket", { 0.30f, 0.46f, 0.24f } },
          { "jacket_dark", { 0.20f, 0.32f, 0.17f } },
          { "cap", { 0.93f, 0.52f, 0.12f } },
          { "cap_dark", { 0.76f, 0.38f, 0.08f } },
          { "denim", { 0.55f, 0.50f, 0.38f } },
          { "denim_dark", { 0.44f, 0.40f, 0.30f } },
          { "pack", { 0.50f, 0.40f, 0.27f } },
          { "pack_dark", { 0.38f, 0.30f, 0.20f } },
          { "bedroll", { 0.24f, 0.38f, 0.56f } },
          { "hair", { 0.80f, 0.62f, 0.30f } },
        } },
        // rust_jacket: falu-rust jacket, cream cap, dark charcoal trousers
        { {
          { "jacket", { 0.68f, 0.26f, 0.18f } },
          { "jacket_dark", { 0.50f, 0.18f, 0.12f } },
          { "cap", { 0.90f, 0.85f, 0.72f } },
          { "cap_dark", { 0.72f, 0.67f, 0.55f } },
          { "denim", { 0.28f, 0.30f, 0.34f } },
          { "denim_dark", { 0.20f, 0.22f, 0.25f } },
          { "pack", { 0.36f, 0.42f, 0.30f } },
          { "pack_dark", { 0.26f, 0.31f, 0.22f } },
          { "bedroll", { 0.78f, 0.64f, 0.26f } },
          { "hair", { 0.28f, 0.20f, 0.14f } },
        } },
        // ochre_jacket: mustard-ochre jacket, navy cap, slate trousers
        { {
          { "jacket", { 0.82f, 0.62f, 0.18f } },
          { "jacket_dark", { 0.62f, 0.45f, 0.12f } },
          { "cap", { 0.18f, 0.28f, 0.46f } },
          { "cap_dark", { 0.12f, 0.19f, 0.34f } },
          { "denim", { 0.35f, 0.40f, 0.44f } },
          { "denim_dark", { 0.26f, 0.30f, 0.34f } },
          { "pack", { 0.46f, 0.32f, 0.24f } },
          { "pack_dark", { 0.34f, 0.22f, 0.16f } },
          { "bedroll", { 0.56f, 0.24f, 0.22f } },
          { "hair", { 0.52f, 0.34f, 0.18f } },
        } },
        // plum_jacket: heather-plum jacket, sage cap, warm grey trousers
        { {
          { "jacket", { 0.48f, 0.28f, 0.46f } },
          { "jacket_dark", { 0.34f, 0.19f, 0.33f } },
          { "cap", { 0.58f, 0.68f, 0.54f } },
          { "cap_dark", { 0.44f, 0.52f, 0.40f } },
          { "denim", { 0.46f, 0.44f, 0.42f } },
          { "denim_dark", { 0.34f, 0.33f, 0.31f } },
          { "pack", { 0.60f, 0.48f, 0.32f } },
          { "pack_dark", { 0.46f, 0.36f, 0.23f } },
          { "bedroll", { 0.30f, 0.48f, 0.42f } },
          { "hair", { 0.20f, 0.16f, 0.14f } },
        } },
        // teal_jacket: fjord-teal jacket, yellow cap, olive trousers
        { {
          { "jacket", { 0.16f, 0.50f, 0.52f } },
          { "jacket_dark", { 0.10f, 0.36f, 0.38f } },
          { "cap", { 0.90f, 0.74f, 0.22f } },
          { "cap_dark", { 0.72f, 0.56f, 0.14f } },
          { "denim", { 0.40f, 0.42f, 0.32f } },
          { "denim_dark", { 0.30f, 0.32f, 0.24f } },
          { "pack", { 0.56f, 0.34f, 0.24f } },
          { "pack_dark", { 0.42f, 0.24f, 0.17f } },
          { "bedroll", { 0.82f, 0.42f, 0.22f } },
          { "hair", { 0.72f, 0.54f, 0.26f } },
        } },
        // pine_jacket: dark pine jacket, rust cap, sand trousers
        { {
          { "jacket", { 0.18f, 0.34f, 0.28f } },
          { "jacket_dark", { 0.11f, 0.24f, 0.19f } },
          { "cap", { 0.76f, 0.32f, 0.20f } },
          { "cap_dark", { 0.58f, 0.22f, 0.13f } },
          { "denim", { 0.62f, 0.56f, 0.44f } },
          { "denim_dark", { 0.48f, 0.43f, 0.33f } },
          { "pack", { 0.34f, 0.32f, 0.38f } },
          { "pack_dark", { 0.24f, 0.22f, 0.28f } },
          { "bedroll", { 0.76f, 0.68f, 0.36f } },
          { "hair", { 0.42f, 0.28f, 0.16f } },
        } },
        // berry_jacket: lingonberry jacket, slate cap, dark denim trousers
        { {
          { "jacket", { 0.68f, 0.22f, 0.34f } },
          { "jacket_dark", { 0.48f, 0.14f, 0.24f } },
          { "cap", { 0.34f, 0.42f, 0.48f } },
          { "cap_dark", { 0.24f, 0.30f, 0.36f } },
          { "denim", { 0.24f, 0.30f, 0.40f } },
          { "denim_dark", { 0.17f, 0.22f, 0.30f } },
          { "pack", { 0.54f, 0.46f, 0.34f } },
          { "pack_dark", { 0.40f, 0.34f, 0.24f } },
          { "bedroll", { 0.38f, 0.52f, 0.30f } },
          { "hair", { 0.86f, 0.72f, 0.44f } },
        } },
      } };
  }

  DisplayColor outfit_colour (std::size_t colour, Outfit outfit) {
    const std::size_t index = static_cast<std::size_t> (outfit);
    if (index > 0 && index < outfit_count)
      for (const Repaint& repaint : repainted_outfits[index - 1])
        if (repaint.name == colour_names[colour])
          return repaint.colour;
    return colours[colour];
  }

  void draw (render::DrawList& dl, const AvatarSkeleton& k, Outfit outfit) {
    // Each outfit's paints, looked up by name once.
    static const std::array<std::vector<DisplayColor>, outfit_count> wardrobe =
      [] {
        std::array<std::vector<DisplayColor>, outfit_count> outfits;
        for (std::size_t o = 0; o < outfits.size (); ++o)
          for (std::size_t c = 0; c < colours.size (); ++c)
            outfits[o].push_back (outfit_colour (c, static_cast<Outfit> (o)));
        return outfits;
      }();
    const std::vector<DisplayColor>& paints =
      wardrobe[static_cast<std::size_t> (outfit) % outfit_count];

    static thread_local std::vector<Vec3> points, normals;
    skin (k, points, normals);

    // The figure sets its own shading rather than inheriting whatever the
    // list last drew: after the unlit blob shadow it would otherwise come
    // out as flat albedo, untouched by the sun.
    dl.set_texture (nullptr);
    dl.state (render::DrawState ());
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);
    for (const Triangle& t : triangles) {
      dl.color (paints[t.colour]);
      for (const std::uint16_t i : t.vertex) {
        dl.normal (normals[i]);
        dl.vertex (points[i]);
      }
    }
    dl.end ();
  }
}
