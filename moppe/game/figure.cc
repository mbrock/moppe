#include <moppe/game/figure.hh>

#include <vector>

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
    Frame frame (const Vec3& origin, const Vec3& axis, const Vec3& reference) {
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
      return { arr (origin), arr (cross (y, z)), arr (y), arr (z) };
    }

    // The rigid motion carrying a rest frame onto a posed one.
    struct Motion {
      Vec3 x, y, z, offset;

      Vec3 apply (const Vec3& p) const {
        return x * p[0] + y * p[1] + z * p[2] + offset;
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
    std::array<Frame, bone_count> f;
    f[pelvis] = frame (k.pelvis, k.waist - k.pelvis, k.forward);
    f[spine] = frame (k.waist, k.chest - k.waist, k.chest_forward);
    f[chest] = frame (k.chest, k.neck - k.chest, k.chest_forward);
    f[head] = frame (k.neck, k.head_up, k.head_forward);
    const Vec3 chest_right = cross (k.chest_up, k.chest_forward);
    for (int i = 0; i < 2; ++i) {
      f[thigh + i] = frame (k.hip[i], k.knee[i] - k.hip[i], k.right);
      f[shin + i] = frame (k.knee[i], k.ankle[i] - k.knee[i], k.right);
      // The foot runs heel to toe and is rolled to its instep.
      const Vec3 sole = k.toe[i] - k.heel[i];
      const Vec3 mid = (k.heel[i] + k.toe[i]) * 0.5f;
      f[foot + i] = frame (k.ankle[i], sole, k.ankle[i] - mid);
      f[upper_arm + i] =
        frame (k.shoulder[i], k.elbow[i] - k.shoulder[i], chest_right);
      f[forearm + i] = frame (k.elbow[i], k.wrist[i] - k.elbow[i], chest_right);
      f[hand + i] = frame (k.wrist[i], k.wrist[i] - k.elbow[i], chest_right);
    }
    return f;
  }

  void draw (render::DrawList& dl, const AvatarSkeleton& k) {
    const std::array<Frame, bone_count> posed = pose (k);
    std::array<Motion, bone_count> moves;
    for (int b = 0; b < bone_count; ++b)
      moves[b] = motion (rest[b], posed[b]);

    static thread_local std::vector<Vec3> skinned;
    skinned.resize (vertices.size ());
    for (std::size_t i = 0; i < vertices.size (); ++i) {
      const Vertex& v = vertices[i];
      const Vec3 p = vec (v.position);
      const Vec3 a = moves[v.bone[0]].apply (p);
      skinned[i] =
        v.bone[1] == v.bone[0]
          ? a
          : a * v.weight + moves[v.bone[1]].apply (p) * (1.0f - v.weight);
    }

    dl.set_texture (nullptr);
    dl.begin (render::Prim::Triangles);
    for (const Triangle& t : triangles) {
      const Vec3& a = skinned[t.vertex[0]];
      const Vec3& b = skinned[t.vertex[1]];
      const Vec3& c = skinned[t.vertex[2]];
      Vec3 n = cross (b - a, c - a);
      if (length2 (n) < 1e-14f)
        continue;
      dl.color (colours[t.colour]);
      dl.normal (normalized (n));
      dl.vertex (a);
      dl.vertex (b);
      dl.vertex (c);
    }
    dl.end ();
  }
}
