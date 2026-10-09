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

  void skin (const AvatarSkeleton& k,
             std::vector<Vec3>& points,
             std::vector<Vec3>& normals) {
    const std::array<Frame, bone_count> posed = pose (k);
    std::array<Motion, bone_count> moves;
    for (int b = 0; b < bone_count; ++b)
      moves[b] = motion (rest[b], posed[b]);

    const Vec3 origin = k.pelvis;
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

  void draw (render::DrawList& dl, const AvatarSkeleton& k) {
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
      dl.color (colours[t.colour]);
      for (const std::uint16_t i : t.vertex) {
        dl.normal (normals[i]);
        dl.vertex (points[i]);
      }
    }
    dl.end ();
  }
}
