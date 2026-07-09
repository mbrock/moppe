#include <moppe/game/blob_shadow.hh>
#include <moppe/game/sprites.hh>

#include <algorithm>

namespace moppe {
namespace game {
  void
  BlobShadow::load (render::Renderer& r) {
    m_tex = make_soft_disc_texture (r);
  }

  void
  BlobShadow::draw (render::DrawList& dl, const map::HeightMap& map,
		    const Vector3D& pos, float radius)
  {
    const float gy = map.interpolated_height (pos.x, pos.z);
    const float h = pos.y - gy;
    if (h > 30.0f || h < -2.0f)
      return; // too high to matter (or underground somehow)

    const float fade = 1.0f - std::max (0.0f, h) / 30.0f;
    const float r = radius * (1.0f + 0.5f * (1.0f - fade));

    // Tangent frame from the interpolated ground normal, so the
    // disc lies flat on slopes
    Vector3D n = map.interpolated_normal (pos.x, pos.z);
    Vector3D t1 = n.cross (Vector3D (1, 0, 0));
    if (t1.length2 () < 0.01f)
      t1 = n.cross (Vector3D (0, 0, 1));
    t1.normalize ();
    Vector3D t2 = n.cross (t1);

    const Vector3D c = Vector3D (pos.x, gy, pos.z) + n * 0.14f;

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
