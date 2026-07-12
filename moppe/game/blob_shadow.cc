#include <moppe/game/blob_shadow.hh>
#include <moppe/game/sprites.hh>

#include <algorithm>

namespace moppe {
  namespace game {
    void BlobShadow::load (render::Renderer& r) {
      m_tex = make_soft_disc_texture (r);
    }

    void BlobShadow::draw (render::DrawList& dl,
                           const map::HeightMap& map,
                           const Vec3& pos,
                           float radius) {
      const float gy = map.interpolated_height (pos[0], pos[2]);
      const float h = pos[1] - gy;
      if (h > 30.0f || h < -2.0f)
        return; // too high to matter (or underground somehow)

      const float fade = 1.0f - std::max (0.0f, h) / 30.0f;
      const float r = radius * (1.0f + 0.5f * (1.0f - fade));

      // Tangent frame from the interpolated ground normal, so the
      // disc lies flat on slopes
      Vec3 n = map.interpolated_normal (pos[0], pos[2]);
      Vec3 t1 = cross (n, Vec3 (1, 0, 0));
      if (length2 (t1) < 0.01f)
        t1 = cross (n, Vec3 (0, 0, 1));
      normalize (t1);
      Vec3 t2 = cross (n, t1);

      const Vec3 c = Vec3 (pos[0], gy, pos[2]) + n * 0.14f;

      render::DrawState soft;
      soft.blend = true;
      soft.depth_write = false;
      soft.cull = false;
      dl.state (soft);
      dl.lit (false);
      dl.set_texture (m_tex.get ());

      dl.color (0.0f, 0.0f, 0.0f, 0.45f * fade);
      dl.begin (render::Prim::Quads);
      dl.uv (0, 0);
      dl.vertex (c - t1 * r - t2 * r);
      dl.uv (1, 0);
      dl.vertex (c + t1 * r - t2 * r);
      dl.uv (1, 1);
      dl.vertex (c + t1 * r + t2 * r);
      dl.uv (0, 1);
      dl.vertex (c - t1 * r + t2 * r);
      dl.end ();

      dl.set_texture (0);
      dl.lit (true);
      dl.state (render::DrawState ());
    }
  }
}
