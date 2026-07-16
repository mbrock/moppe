#include <moppe/game/trail_surface.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace moppe::game {
  namespace {
    constexpr std::array<float, 7> cross_positions = { -1.18f, -1.0f, -0.46f,
                                                       0.0f,   0.46f, 1.0f,
                                                       1.18f };
    constexpr std::array<float, 7> cross_opacity = { 0.0f,  0.48f, 0.64f, 0.68f,
                                                     0.64f, 0.48f, 0.0f };
    constexpr std::array<float, 7> cross_value = { 0.82f, 0.88f, 0.72f, 1.0f,
                                                   0.72f, 0.88f, 0.82f };

    struct RibbonVertex {
      Vec3 position;
      Vec3 normal;
      float u;
      float distance_m;
      float opacity;
      float value;
    };

    using RibbonRow = std::array<RibbonVertex, cross_positions.size ()>;

    float trail_smoothstep (float low, float high, float value) {
      const float t = std::clamp ((value - low) / (high - low), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    terrain::TrailAlignmentPoint
    nearest_image (terrain::TrailAlignmentPoint point,
                   terrain::TrailAlignmentPoint reference,
                   const map::HeightMap& map) {
      if (!map.periodic ())
        return point;
      const Vec3 period = map.size ();
      point.x_m =
        reference.x_m + std::remainder (point.x_m - reference.x_m, period[0]);
      point.z_m =
        reference.z_m + std::remainder (point.z_m - reference.z_m, period[2]);
      return point;
    }

    RibbonRow make_row (const map::HeightMap& map,
                        terrain::TrailAlignmentPoint before,
                        terrain::TrailAlignmentPoint center,
                        terrain::TrailAlignmentPoint after,
                        float half_width,
                        float distance_m) {
      Vec3 tangent (after.x_m - before.x_m, 0.0f, after.z_m - before.z_m);
      if (length2 (tangent) < 1e-6f)
        tangent = Vec3 (0, 0, 1);
      else
        tangent = normalized (tangent);
      const Vec3 across (-tangent[2], 0.0f, tangent[0]);
      RibbonRow row;
      for (std::size_t cross = 0; cross < row.size (); ++cross) {
        Vec3 position (center.x_m, 0.0f, center.z_m);
        position += across * (cross_positions[cross] * half_width);
        position[1] =
          map.interpolated_height (position[0], position[2]) + 0.035f;
        Vec3 normal = map.interpolated_normal (position[0], position[2]);
        normalize (normal);
        const float normalized_height = position[1] / map.size ()[1];
        const float snow = trail_smoothstep (0.55f, 0.68f, normalized_height) *
                           trail_smoothstep (0.58f, 0.78f, normal[1]);
        row[cross] = { .position = position,
                       .normal = normal,
                       .u = 0.5f * (cross_positions[cross] + 1.0f),
                       .distance_m = distance_m,
                       .opacity = cross_opacity[cross] * (1.0f - snow),
                       .value = cross_value[cross] };
      }
      return row;
    }

    void emit (render::DrawList& draw, const RibbonVertex& vertex) {
      draw.color (0.70f * vertex.value,
                  0.46f * vertex.value,
                  0.23f * vertex.value,
                  vertex.opacity);
      draw.normal (vertex.normal);
      draw.uv (vertex.u, vertex.distance_m * 0.28f);
      draw.vertex (vertex.position);
    }
  }

  render::DrawList build_trail_ribbon (const map::HeightMap& map,
                                       const terrain::TrailNetwork& trail) {
    render::DrawList draw;
    const auto& points = trail.alignment.points;
    if (points.size () < 3 || trail.formed_width <= 0.0f * mp_units::si::metre)
      return draw;

    render::DrawState state;
    state.blend = true;
    state.depth_write = false;
    state.cull = false;
    state.depth_bias = true;
    draw.state (state);
    draw.lit (true);
    draw.fogged (true);

    const float half_width = 0.5f * meters_value (trail.formed_width);
    std::vector<RibbonRow> rows;
    rows.reserve (points.size () + 1);
    float distance_m = 0.0f;
    for (std::size_t point = 0; point < points.size (); ++point) {
      const auto center = points[point];
      const auto before = nearest_image (
        points[(point + points.size () - 1) % points.size ()], center, map);
      const auto after =
        nearest_image (points[(point + 1) % points.size ()], center, map);
      if (point > 0)
        distance_m += std::hypot (center.x_m - points[point - 1].x_m,
                                  center.z_m - points[point - 1].z_m);
      rows.push_back (
        make_row (map, before, center, after, half_width, distance_m));
    }
    const auto closure = nearest_image (points.front (), points.back (), map);
    distance_m += std::hypot (closure.x_m - points.back ().x_m,
                              closure.z_m - points.back ().z_m);
    const auto closure_before = points.back ();
    const auto closure_after = nearest_image (points[1], closure, map);
    rows.push_back (make_row (
      map, closure_before, closure, closure_after, half_width, distance_m));

    draw.begin (render::Prim::Triangles);
    for (std::size_t row = 0; row + 1 < rows.size (); ++row)
      for (std::size_t cross = 0; cross + 1 < rows[row].size (); ++cross) {
        emit (draw, rows[row][cross]);
        emit (draw, rows[row + 1][cross]);
        emit (draw, rows[row + 1][cross + 1]);
        emit (draw, rows[row][cross]);
        emit (draw, rows[row + 1][cross + 1]);
        emit (draw, rows[row][cross + 1]);
      }
    draw.end ();
    return draw;
  }

  void TrailSurface::rebuild (render::Renderer& renderer,
                              const map::HeightMap& map,
                              const terrain::TrailNetwork& trail) {
    m_mesh = renderer.create_mesh (build_trail_ribbon (map, trail));
    m_period = map.size ();
    m_periodic = map.periodic ();
  }

  void TrailSurface::clear () {
    m_mesh.reset ();
  }

  void TrailSurface::draw (render::Renderer& renderer,
                           const Vec3& camera) const {
    if (!m_mesh)
      return;
    if (!m_periodic) {
      renderer.draw_mesh (*m_mesh, Mat4 ());
      return;
    }
    const float base_x = std::floor (camera[0] / m_period[0]) * m_period[0];
    const float base_z = std::floor (camera[2] / m_period[2]) * m_period[2];
    for (int z = -1; z <= 1; ++z)
      for (int x = -1; x <= 1; ++x)
        renderer.draw_mesh (
          *m_mesh,
          Mat4::translation (
            Vec3 (base_x + x * m_period[0], 0.0f, base_z + z * m_period[2])));
  }
}
