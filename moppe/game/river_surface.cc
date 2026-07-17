#include <moppe/game/river_surface.hh>

#include <moppe/terrain/river.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace moppe::game {
  namespace river_surface_detail {
    constexpr float depth_span_m = 2.5f;
    constexpr std::array<float, 7> cross_positions = { -1.15f, -1.0f, -0.54f,
                                                       0.0f,   0.54f, 1.0f,
                                                       1.15f };
    constexpr std::array<float, 7> cross_opacity = { 0.0f,  0.62f, 0.92f, 1.0f,
                                                     0.92f, 0.62f, 0.0f };

    struct SurfacePoint {
      Vec3 position;
      Vec3 tangent;
      float width_m;
      float flow_distance_m;
      float rapid;
      float waterfall;
      float opacity;
      // Signed plan-view curvature in 1/m; positive when the channel bends
      // toward the +across direction, i.e. the bend center lies on the
      // +across side.
      float curvature_across;
    };

    struct RibbonVertex {
      Vec3 position;
      Vec3 normal;
      float u;
      float flow_distance_m;
      float rapid;
      float depth;
      float waterfall;
      float opacity;
    };

    using RibbonRow = std::array<RibbonVertex, cross_positions.size ()>;

    SurfacePoint nearest_image (SurfacePoint point,
                                const SurfacePoint& reference,
                                const map::HeightMap& map) {
      if (!map.periodic ())
        return point;
      const Vec3 period = map.size ();
      point.position[0] =
        reference.position[0] +
        std::remainder (point.position[0] - reference.position[0], period[0]);
      point.position[2] =
        reference.position[2] +
        std::remainder (point.position[2] - reference.position[2], period[2]);
      return point;
    }

    float smoothstep (float low, float high, float value) {
      const float t = std::clamp ((value - low) / (high - low), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    float rapid_signal (float slope) {
      return std::clamp ((slope - 0.035f) / 0.24f, 0.0f, 1.0f);
    }

    std::vector<SurfacePoint>
    make_surface_points (const map::HeightMap& map,
                         const terrain::RiverAlignment& alignment,
                         bool fade_source) {
      std::vector<SurfacePoint> result;
      const auto& points = alignment.points;
      result.reserve (points.size ());
      float mouth_distance_m = std::numeric_limits<float>::infinity ();
      bool falling_mouth = false;
      for (std::size_t point = 0; point < points.size (); ++point)
        if (points[point].standing_water >= 0.99f) {
          mouth_distance_m = points[point].distance_m;
          // The standing-water interpolation itself may have already hidden
          // the steep routed cell. Inspect the approach to the mouth, not
          // just its final sample, before deciding whether this is a fall.
          for (std::size_t approach = 0; approach < point; ++approach)
            if (mouth_distance_m - points[approach].distance_m <= 48.0f)
              falling_mouth |= points[approach].waterfall > 0.5f ||
                               points[approach].slope > 0.4f;
          break;
        }
      for (std::size_t point = 0; point < points.size (); ++point) {
        const auto& before = points[point ? point - 1 : point];
        const auto& center = points[point];
        const auto& after =
          points[point + 1 < points.size () ? point + 1 : point];
        Vec3 tangent (after.x_m - before.x_m, 0.0f, after.z_m - before.z_m);
        if (length2 (tangent) < 1e-6f)
          tangent = Vec3 (0, 0, 1);
        else
          tangent = normalized (tangent);
        const Vec3 across (-tangent[2], 0.0f, tangent[0]);
        const auto area = center.contributing_area_m2 * mp_units::si::metre *
                          mp_units::si::metre;
        const float width_m = meters_value (terrain::river_width (area));
        const float fill_depth =
          0.72f * meters_value (terrain::river_depth (area));
        const float center_ground =
          map.interpolated_height (center.x_m, center.z_m);
        const float bank_distance = 0.5f * width_m + 0.75f;
        const float left_bank =
          map.interpolated_height (center.x_m - across[0] * bank_distance,
                                   center.z_m - across[2] * bank_distance);
        const float right_bank =
          map.interpolated_height (center.x_m + across[0] * bank_distance,
                                   center.z_m + across[2] * bank_distance);
        const float bank_limit = std::min (left_bank, right_bank) - 0.08f;
        const float running_surface =
          std::max (center_ground + 0.035f,
                    std::min (center_ground + fill_depth, bank_limit));
        const float water_blend =
          smoothstep (0.0f, 1.0f, center.standing_water);
        float surface =
          running_surface +
          (center.water_level_m + 0.045f - running_surface) * water_blend;
        // A plan-view spline can bow off the routed cell centers and cross a
        // small shoulder. Keep the useful interior of the cross-section above
        // the terrain it actually spans; the outer rows still intersect the
        // banks naturally and feather the visible waterline.
        for (const float cross : { -0.54f, 0.0f, 0.54f }) {
          const float ground = map.interpolated_height (
            center.x_m + across[0] * cross * 0.5f * width_m,
            center.z_m + across[2] * cross * 0.5f * width_m);
          surface = std::max (surface, ground + 0.045f);
        }
        const float source_opacity =
          fade_source ? smoothstep (0.0f,
                                    std::max (8.0f, 1.5f * width_m),
                                    center.distance_m)
                      : 1.0f;
        float mouth_opacity = 1.0f;
        if (std::isfinite (mouth_distance_m)) {
          // Retire the ribbon just before the standing-water geometry owns
          // the surface. This hides both coplanar overlap at ordinary mouths
          // and a false tilted sheet across a coastal knickpoint.
          const float lip_clearance = falling_mouth
                                        ? std::max (8.0f, 0.75f * width_m)
                                        : std::max (3.0f, 0.35f * width_m);
          const float fade_length = falling_mouth
                                      ? std::max (8.0f, width_m)
                                      : std::max (12.0f, 1.5f * width_m);
          const float fade_end = mouth_distance_m - lip_clearance;
          mouth_opacity =
            1.0f -
            smoothstep (fade_end - fade_length, fade_end, center.distance_m);
          mouth_opacity *= mouth_opacity;
          if (falling_mouth)
            mouth_opacity *= mouth_opacity;
        }
        result.push_back (
          { .position = Vec3 (center.x_m, surface, center.z_m),
            .tangent = tangent,
            .width_m = width_m,
            .flow_distance_m = center.flow_distance_m,
            .rapid = rapid_signal (center.slope),
            .waterfall = center.waterfall,
            .opacity =
              source_opacity * mouth_opacity *
              (1.0f - smoothstep (0.05f, 0.98f, center.standing_water)),
            .curvature_across = 0.0f });
      }

      // Raise upstream samples over any downstream shoulder instead of
      // lowering downstream water into the terrain. This is the least-change
      // monotone envelope: flow never appears uphill and the ribbon never
      // develops periodic dry holes along a smooth valley.
      for (std::size_t point = result.size () - 1; point > 0; --point)
        result[point - 1].position[1] =
          std::max (result[point - 1].position[1], result[point].position[1]);

      // Signed bend curvature from the turn rate of neighbouring tangents;
      // positive bends toward +across. Two smoothing passes keep the reading
      // from inheriting knot-scale wiggle from the alignment spline.
      for (std::size_t point = 1; point + 1 < result.size (); ++point) {
        const Vec3& before = result[point - 1].tangent;
        const Vec3& after = result[point + 1].tangent;
        const float run = 0.5f * (result[point + 1].flow_distance_m -
                                  result[point - 1].flow_distance_m);
        if (run > 1e-3f)
          result[point].curvature_across =
            -(before[2] * after[0] - before[0] * after[2]) / run;
      }
      for (int pass = 0; pass < 2; ++pass) {
        float previous =
          result.empty () ? 0.0f : result.front ().curvature_across;
        for (std::size_t point = 1; point + 1 < result.size (); ++point) {
          const float smoothed = 0.25f * previous +
                                 0.5f * result[point].curvature_across +
                                 0.25f * result[point + 1].curvature_across;
          previous = result[point].curvature_across;
          result[point].curvature_across = smoothed;
        }
      }
      return result;
    }

    RibbonRow make_row (const map::HeightMap& map,
                        const std::vector<SurfacePoint>& points,
                        std::size_t point) {
      const SurfacePoint& center = points[point];
      const SurfacePoint& before = points[point ? point - 1 : point];
      const SurfacePoint& after =
        points[point + 1 < points.size () ? point + 1 : point];
      const Vec3 across (-center.tangent[2], 0.0f, center.tangent[0]);
      const float run = std::hypot (after.position[0] - before.position[0],
                                    after.position[2] - before.position[2]);
      const float grade =
        run > 1e-5f ? (after.position[1] - before.position[1]) / run : 0.0f;
      // Superelevation: moving water banks into its bends, so the shading
      // normal leans toward the bend center by the centripetal crossfall
      // v^2 k / g, exaggerated for legibility at ribbon scale.
      const float speed = 2.0f + 3.5f * center.rapid + 5.0f * center.waterfall;
      const float lean = std::clamp (
        speed * speed * center.curvature_across / 9.81f * 3.0f, -0.22f, 0.22f);
      const Vec3 normal = normalized (
        Vec3 (-center.tangent[0] * grade, 1.0f, -center.tangent[2] * grade) +
        across * lean);
      // Thalweg asymmetry: the deep line migrates to the outside of each
      // bend, so depth reads dark against the outer bank and glassy over the
      // inner point bar.
      const float thalweg_shift = std::clamp (
        center.curvature_across * center.width_m * 1.2f, -0.45f, 0.45f);
      const float half_width = 0.5f * center.width_m;
      RibbonRow row;
      for (std::size_t cross = 0; cross < row.size (); ++cross) {
        Vec3 position =
          center.position + across * (cross_positions[cross] * half_width);
        const float ground = map.interpolated_height (position[0], position[2]);
        const float depth =
          std::clamp ((position[1] - ground) / depth_span_m *
                        (1.0f - thalweg_shift * cross_positions[cross]),
                      0.0f,
                      1.0f);
        row[cross] = { .position = position,
                       .normal = normal,
                       .u = 0.5f * (cross_positions[cross] + 1.0f),
                       .flow_distance_m = center.flow_distance_m,
                       .rapid = center.rapid,
                       .depth = depth,
                       .waterfall = center.waterfall,
                       .opacity = center.opacity * cross_opacity[cross] };
      }
      return row;
    }

    void emit (render::DrawList& draw, const RibbonVertex& vertex) {
      draw.color (vertex.rapid, vertex.depth, vertex.waterfall, vertex.opacity);
      draw.normal (vertex.normal);
      draw.uv (vertex.u, vertex.flow_distance_m);
      draw.vertex (vertex.position);
    }
  }

  render::DrawList build_river_ribbons (const map::HeightMap& map,
                                        const terrain::RiverNetwork& rivers) {
    render::DrawList draw;
    render::DrawState state;
    state.blend = true;
    state.depth_write = false;
    state.cull = false;
    draw.state (state);
    draw.lit (false);
    draw.fogged (true);

    std::vector<std::size_t> upstream_reaches (rivers.reaches.size (), 0);
    for (const terrain::RiverReach& reach : rivers.reaches)
      if (reach.downstream_reach != terrain::RiverReach::no_id)
        ++upstream_reaches[reach.downstream_reach];

    std::vector<std::vector<river_surface_detail::SurfacePoint>> reach_points (
      rivers.reaches.size ());
    for (const terrain::RiverReach& reach : rivers.reaches) {
      const bool fade_source = upstream_reaches[reach.id] == 0 &&
                               reach.upstream_body == terrain::no_water_body;
      reach_points[reach.id] = river_surface_detail::make_surface_points (
        map, reach.alignment, fade_source);
    }

    // Reaches share topology but build their bank envelopes independently.
    // Reconcile the one shared section, then add a zero-length rotation row
    // to cover the wedge between tributary and trunk tangents.
    for (const terrain::RiverReach& reach : rivers.reaches) {
      if (reach.downstream_reach == terrain::RiverReach::no_id ||
          reach_points[reach.id].empty () ||
          reach_points[reach.downstream_reach].empty ())
        continue;
      auto& end = reach_points[reach.id].back ();
      auto& start = reach_points[reach.downstream_reach].front ();
      const float junction_height =
        std::max (end.position[1], start.position[1]);
      end.position[1] = junction_height;
      start.position[1] = junction_height;
      auto& upstream = reach_points[reach.id];
      for (std::size_t point = upstream.size () - 1; point > 0; --point)
        upstream[point - 1].position[1] = std::max (
          upstream[point - 1].position[1], upstream[point].position[1]);
    }

    draw.begin (render::Prim::Triangles);
    for (const terrain::RiverReach& reach : rivers.reaches) {
      std::vector<river_surface_detail::SurfacePoint> points =
        reach_points[reach.id];
      if (reach.downstream_reach != terrain::RiverReach::no_id &&
          !points.empty () && !reach_points[reach.downstream_reach].empty ()) {
        auto transition = river_surface_detail::nearest_image (
          reach_points[reach.downstream_reach].front (), points.back (), map);
        transition.opacity =
          std::min (transition.opacity, points.back ().opacity);
        points.push_back (transition);
      }
      if (points.size () < 2)
        continue;
      std::vector<river_surface_detail::RibbonRow> rows;
      rows.reserve (points.size ());
      for (std::size_t point = 0; point < points.size (); ++point)
        rows.push_back (river_surface_detail::make_row (map, points, point));
      for (std::size_t row = 0; row + 1 < rows.size (); ++row)
        for (std::size_t cross = 0; cross + 1 < rows[row].size (); ++cross) {
          river_surface_detail::emit (draw, rows[row][cross]);
          river_surface_detail::emit (draw, rows[row + 1][cross]);
          river_surface_detail::emit (draw, rows[row + 1][cross + 1]);
          river_surface_detail::emit (draw, rows[row][cross]);
          river_surface_detail::emit (draw, rows[row + 1][cross + 1]);
          river_surface_detail::emit (draw, rows[row][cross + 1]);
        }
    }
    draw.end ();
    return draw;
  }

  void RiverSurface::rebuild (render::Renderer& renderer,
                              const map::HeightMap& map,
                              const terrain::RiverNetwork& rivers) {
    m_mesh = renderer.create_mesh (build_river_ribbons (map, rivers));
    m_period = map.size ();
    m_periodic = map.periodic ();
  }

  void RiverSurface::clear () {
    m_mesh.reset ();
  }

  void RiverSurface::draw (render::Renderer& renderer,
                           const Vec3& camera) const {
    if (!m_mesh)
      return;
    if (!m_periodic) {
      renderer.draw_rivers (*m_mesh, Mat4 ());
      return;
    }
    const float base_x = std::floor (camera[0] / m_period[0]) * m_period[0];
    const float base_z = std::floor (camera[2] / m_period[2]) * m_period[2];
    for (int z = -1; z <= 1; ++z)
      for (int x = -1; x <= 1; ++x)
        renderer.draw_rivers (
          *m_mesh,
          Mat4::translation (
            Vec3 (base_x + x * m_period[0], 0.0f, base_z + z * m_period[2])));
  }
}
