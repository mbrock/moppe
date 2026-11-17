#include <moppe/game/blob_shadow.hh>
#include <moppe/game/sprites.hh>

#include <algorithm>

namespace moppe {
  namespace game {
    void BlobShadow::load (render::Renderer& r) {
      m_tex = make_soft_disc_texture (r);
    }

    void BlobShadow::draw (render::DrawList& dl,
                           const map::SurfaceGeometry& surface,
                           const position_t& pos,
                           meters_t radius) {
      const position_t under = at_height (plan (pos), 0.0f * u::m);
      const meters_t gy =
        terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (surface, under)) *
        u::m;
      const meters_t h = position_value (pos)[1] * u::m - gy;
      if (h > 30.0f * u::m || h < -2.0f * u::m)
        return; // too high to matter (or underground somehow)

      const proportion_t fade =
        (1.0f - meters_value (std::max (0.0f * u::m, h)) / 30.0f) *
        proportion[one];
      const meters_t r =
        radius * (1.0f + 0.5f * (1.0f - fade.numerical_value_in (one)));

      // Tangent frame from the interpolated ground normal, so the
      // disc lies flat on slopes
      const direction_t n = unit_direction (
        spatial::sample<terrain::terrain_normal> (surface, under)
          .numerical_value_in (mp_units::one));
      const direction_t t1 = [&] {
        const Vec3 across = cross (direction_value (n), Vec3 (1, 0, 0));
        return unit_direction (normalized (
          length2 (across) < 0.01f ? cross (direction_value (n), Vec3 (0, 0, 1))
                                   : across));
      }();
      const direction_t t2 =
        unit_direction (cross (direction_value (n), direction_value (t1)));

      const position_t c =
        at_height (plan (pos), gy) +
        quantity_cast<isq::position_vector> (along (n, 0.14f * u::m));
      const auto offset = [] (const direction_t& way, meters_t distance) {
        return quantity_cast<isq::position_vector> (along (way, distance));
      };

      render::DrawState soft;
      soft.blend = true;
      soft.depth_write = false;
      soft.cull = false;
      dl.state (soft);
      dl.lit (false);
      dl.set_texture (m_tex.get ());

      dl.color (DisplayColor (0.0f, 0.0f, 0.0f), 0.45f * fade);
      dl.begin (render::Prim::Quads);
      dl.uv (0, 0);
      dl.vertex (c - offset (t1, r) - offset (t2, r));
      dl.uv (1, 0);
      dl.vertex (c + offset (t1, r) - offset (t2, r));
      dl.uv (1, 1);
      dl.vertex (c + offset (t1, r) + offset (t2, r));
      dl.uv (0, 1);
      dl.vertex (c - offset (t1, r) + offset (t2, r));
      dl.end ();

      dl.set_texture (0);
      dl.lit (true);
      dl.state (render::DrawState ());
    }

    void BlobShadow::draw (render::DrawList& dl,
                           const map::SurfaceGeometry& surface,
                           const Vec3& pos,
                           float radius) {
      draw (dl, surface, moppe::position (pos), radius * u::m);
    }
  }
}
