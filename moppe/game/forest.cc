#include <moppe/game/forest.hh>
#include <moppe/gfx/signal.hh>

#include <moppe/profile.hh>
#include <moppe/render/draw.hh>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace moppe::game {
  namespace {
    using namespace mp_units;

    constexpr int forest_chunks_per_side = 16;
    constexpr meters_t forest_geometry_reach = 2200.0f * u::m;
    constexpr meters_t forest_detail_reach = 700.0f * u::m;

    std::uint32_t
    forest_hash (std::uint32_t x, std::uint32_t z, std::uint32_t seed) {
      std::uint32_t value = seed ^ (x * 0x9e3779b9U) ^ (z * 0x85ebca6bU);
      value ^= value >> 16;
      value *= 0x7feb352dU;
      value ^= value >> 15;
      value *= 0x846ca68bU;
      value ^= value >> 16;
      return value;
    }

    float hash_unit (std::uint32_t value) {
      return static_cast<float> (value & 0x00ffffffU) /
             static_cast<float> (0x01000000U);
    }

    float hash_lane (std::uint32_t value, std::uint32_t lane) {
      return hash_unit (forest_hash (value, lane, 0x517cc1b7U));
    }

    position_t sample_position (meters_t x, meters_t z) {
      return position (
        Vec3 (x.numerical_value_in (u::m), 0, z.numerical_value_in (u::m)));
    }

    terrain::SurfaceElevation
    elevation_at (const map::SurfaceGeometry& surface, meters_t x, meters_t z) {
      return spatial::sample<terrain::surface_elevation> (
        surface, sample_position (x, z));
    }

    terrain::TerrainNormal
    normal_at (const map::SurfaceGeometry& surface, meters_t x, meters_t z) {
      return spatial::sample<terrain::terrain_normal> (surface,
                                                       sample_position (x, z));
    }

    map::ForestCover
    cover_at (const map::SurfaceReadings& readings, meters_t x, meters_t z) {
      return spatial::sample<map::forest_cover> (readings,
                                                 sample_position (x, z));
    }

    position_t forest_position (meters_t x,
                                terrain::SurfaceElevation elevation,
                                meters_t z) {
      return position (
        Vec3 (x.numerical_value_in (u::m),
              elevation.quantity_from_zero ().numerical_value_in (u::m),
              z.numerical_value_in (u::m)));
    }

    meters_t position_component (position_t value, std::size_t component) {
      return position_value (value)[component] * u::m;
    }

    meters_t extent_component (const spatial_extent_t& value,
                               std::size_t component) {
      return extent_value (value)[component] * u::m;
    }

    bool visible_in_view (const displacement_t& delta,
                          meters_t radius,
                          const ForestView& view) {
      const Vec3& offset = displacement_value (delta);
      const meters_t forward = dot (offset, view.forward) * u::m;
      if (forward < -radius)
        return false;

      const float vertical_tangent =
        moppe::tan (view.vertical_field_of_view / 2.0f);
      const float horizontal_tangent =
        vertical_tangent * scalar_value (view.aspect_ratio);
      const meters_t side = std::abs (dot (offset, view.right)) * u::m;
      const meters_t height = std::abs (dot (offset, view.up)) * u::m;
      const float side_plane_length =
        std::sqrt (1.0f + horizontal_tangent * horizontal_tangent);
      const float top_plane_length =
        std::sqrt (1.0f + vertical_tangent * vertical_tangent);
      return side <=
               forward * horizontal_tangent + radius * side_plane_length &&
             height <= forward * vertical_tangent + radius * top_plane_length;
    }

    void forest_vertex (render::DrawList& draw,
                        const Vec3& point,
                        const Vec3& normal,
                        proportion_t bend,
                        proportion_t flutter) {
      draw.normal (normal);
      draw.wind (bend);
      draw.flutter (flutter);
      draw.vertex (point);
    }

    // A vertex's two wind weights: how far up the plant it sits, and how far
    // out from its stem.
    struct Sway {
      proportion_t bend;
      proportion_t flutter;
    };

    void append_triangle (render::DrawList& draw,
                          Vec3 a,
                          Vec3 b,
                          Vec3 c,
                          const Vec3& outside,
                          Sway sway_a,
                          Sway sway_b,
                          Sway sway_c) {
      Vec3 normal = normalized (cross (b - a, c - a));
      if (dot (normal, outside) < 0.0f) {
        std::swap (b, c);
        std::swap (sway_b, sway_c);
        normal = -normal;
      }
      forest_vertex (draw, a, normal, sway_a.bend, sway_a.flutter);
      forest_vertex (draw, b, normal, sway_b.bend, sway_b.flutter);
      forest_vertex (draw, c, normal, sway_c.bend, sway_c.flutter);
    }

    void append_trunk (render::DrawList& draw,
                       const ForestSite& site,
                       const Vec3& up,
                       const Vec3& across,
                       const Vec3& forward,
                       meters_t height,
                       meters_t radius) {
      const float tint = hash_lane (site.seed, 11);
      const Vec3& root = position_value (site.position);
      const float height_m = height.numerical_value_in (u::m);
      const float radius_m = radius.numerical_value_in (u::m);
      draw.color (
        0.16f + 0.040f * tint, 0.075f + 0.028f * tint, 0.028f + 0.016f * tint);
      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < 3; ++side) {
        const float a0 = 2.0f * std::numbers::pi_v<float> * side / 3.0f;
        const float a1 = 2.0f * std::numbers::pi_v<float> * (side + 1) / 3.0f;
        const Vec3 n0 = across * std::cos (a0) + forward * std::sin (a0);
        const Vec3 n1 = across * std::cos (a1) + forward * std::sin (a1);
        const Vec3 p0 = root + n0 * radius_m;
        const Vec3 p1 = root + n1 * radius_m;
        const Vec3 q0 = p0 + up * height_m;
        const Vec3 q1 = p1 + up * height_m;
        const Vec3 normal = normalized (n0 + n1);
        // Wood leans and does not flutter, whatever the wind does.
        forest_vertex (
          draw, p0, normal, 0.0f * proportion[one], 0.0f * proportion[one]);
        forest_vertex (
          draw, p1, normal, 0.0f * proportion[one], 0.0f * proportion[one]);
        forest_vertex (
          draw, q1, normal, 0.16f * proportion[one], 0.0f * proportion[one]);
        forest_vertex (
          draw, p0, normal, 0.0f * proportion[one], 0.0f * proportion[one]);
        forest_vertex (
          draw, q1, normal, 0.16f * proportion[one], 0.0f * proportion[one]);
        forest_vertex (
          draw, q0, normal, 0.16f * proportion[one], 0.0f * proportion[one]);
      }
      draw.end ();
    }

    void append_broadleaf_crown (render::DrawList& draw,
                                 const ForestSite& site,
                                 const Vec3& up,
                                 const Vec3& across,
                                 const Vec3& forward,
                                 meters_t height,
                                 meters_t radius) {
      constexpr int sides = 5;
      const Vec3& root = position_value (site.position);
      const float height_m = height.numerical_value_in (u::m);
      const float radius_m = radius.numerical_value_in (u::m);
      const Vec3 bottom = root + up * (0.30f * height_m);
      const Vec3 top = root + up * height_m;
      const float turn =
        2.0f * std::numbers::pi_v<float> * hash_lane (site.seed, 29);
      const Vec3 lean = (across * (hash_lane (site.seed, 51) - 0.5f) +
                         forward * (hash_lane (site.seed, 52) - 0.5f)) *
                        (0.22f * radius_m);
      const Vec3 lower_center = root + up * (0.53f * height_m) + lean * 0.45f;
      const Vec3 upper_center = root + up * (0.73f * height_m) + lean;
      Vec3 lower[sides];
      Vec3 upper[sides];
      for (int side = 0; side < sides; ++side) {
        const float angle =
          turn + 2.0f * std::numbers::pi_v<float> * side / sides;
        const Vec3 radial =
          across * std::cos (angle) + forward * std::sin (angle);
        const float lower_radius =
          radius_m * (0.88f + 0.24f * hash_lane (site.seed, 31 + side));
        const float upper_radius =
          radius_m * (0.58f + 0.22f * hash_lane (site.seed, 41 + side));
        lower[side] =
          lower_center + radial * lower_radius +
          up * radius_m * (0.10f * (hash_lane (site.seed, 61 + side) - 0.5f));
        upper[side] =
          upper_center + radial * upper_radius +
          up * radius_m * (0.13f * (hash_lane (site.seed, 71 + side) - 0.5f));
      }

      const float hue = hash_lane (site.seed, 19);
      const float cover = site.cover.numerical_value_in (one);
      draw.color (0.065f + 0.040f * hue,
                  0.19f + 0.10f * cover + 0.040f * hue,
                  0.035f + 0.030f * hue);
      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const Vec3& lower_a = lower[side];
        const Vec3& lower_b = lower[(side + 1) % sides];
        const Vec3& upper_a = upper[side];
        const Vec3& upper_b = upper[(side + 1) % sides];
        const Vec3 lower_outside = (lower_a + lower_b) * 0.5f - lower_center;
        const Vec3 upper_outside = (upper_a + upper_b) * 0.5f - upper_center;
        constexpr Sway on_stem_low { 0.22f * proportion[one],
                                     0.0f * proportion[one] };
        constexpr Sway on_stem_high { 0.92f * proportion[one],
                                      0.10f * proportion[one] };
        constexpr Sway lower_branch { 0.52f * proportion[one],
                                      0.88f * proportion[one] };
        constexpr Sway upper_branch { 0.72f * proportion[one],
                                      1.0f * proportion[one] };
        append_triangle (draw,
                         bottom,
                         lower_b,
                         lower_a,
                         lower_outside,
                         on_stem_low,
                         lower_branch,
                         lower_branch);
        append_triangle (draw,
                         lower_a,
                         lower_b,
                         upper_b,
                         lower_outside,
                         lower_branch,
                         lower_branch,
                         upper_branch);
        append_triangle (draw,
                         lower_a,
                         upper_b,
                         upper_a,
                         upper_outside,
                         lower_branch,
                         upper_branch,
                         upper_branch);
        append_triangle (draw,
                         top,
                         upper_a,
                         upper_b,
                         upper_outside,
                         on_stem_high,
                         upper_branch,
                         upper_branch);
      }
      draw.end ();
    }

    void append_conifer_crown (render::DrawList& draw,
                               const ForestSite& site,
                               const Vec3& up,
                               const Vec3& across,
                               const Vec3& forward,
                               meters_t height,
                               meters_t radius) {
      constexpr int tiers = 3;
      constexpr int sides = 4;
      const float hue = hash_lane (site.seed, 23);
      const float cover = site.cover.numerical_value_in (one);
      const Vec3& root = position_value (site.position);
      const float height_m = height.numerical_value_in (u::m);
      const float radius_m = radius.numerical_value_in (u::m);
      draw.color (0.045f + 0.025f * hue,
                  0.15f + 0.085f * cover + 0.030f * hue,
                  0.035f + 0.025f * hue);
      draw.begin (render::Prim::Triangles);
      for (int tier = 0; tier < tiers; ++tier) {
        // Overlapping whorls expose the trunk between skirts and break the
        // single rigid pyramid into the layered silhouette of a conifer. Each
        // tier turns independently, so their four cheap faces do not line up
        // into one continuous crease.
        const float tier_f = static_cast<float> (tier);
        const float base_height = (0.22f + 0.22f * tier_f) * height_m;
        const float tip_height = (0.70f + 0.18f * tier_f) * height_m;
        const float tier_radius = radius_m * (1.0f - 0.22f * tier_f);
        const float turn =
          2.0f * std::numbers::pi_v<float> * hash_lane (site.seed, 61 + tier);
        const Vec3 base = root + up * base_height;
        const Vec3 tip = root + up * tip_height;
        for (int side = 0; side < sides; ++side) {
          const float a0 =
            turn + 2.0f * std::numbers::pi_v<float> * side / sides;
          const float a1 =
            turn + 2.0f * std::numbers::pi_v<float> * (side + 1) / sides;
          const Vec3 p0 =
            base +
            (across * std::cos (a0) + forward * std::sin (a0)) * tier_radius;
          const Vec3 p1 =
            base +
            (across * std::cos (a1) + forward * std::sin (a1)) * tier_radius;
          const Sway leader { (0.64f + 0.13f * tier_f) * proportion[one],
                              0.14f * proportion[one] };
          const Sway skirt { (0.28f + 0.16f * tier_f) * proportion[one],
                             0.85f * proportion[one] };
          append_triangle (
            draw, tip, p0, p1, (p0 + p1) * 0.5f - base, leader, skirt, skirt);
        }
      }
      draw.end ();
    }

    void append_tree (render::DrawList& draw, const ForestSite& site) {
      const Vec3 surface_normal = site.normal.numerical_value_in (one);
      const Vec3 up =
        normalized (Vec3 (0, 1, 0) * 0.78f + surface_normal * 0.22f);
      const float heading =
        2.0f * std::numbers::pi_v<float> * hash_lane (site.seed, 3);
      Vec3 forward (std::sin (heading), 0.0f, std::cos (heading));
      forward = normalized (forward - up * dot (forward, up));
      const Vec3 across = normalized (cross (up, forward));
      const float cover = site.cover.numerical_value_in (one);
      const float size = site.size.numerical_value_in (one);
      const meters_t height = size * (9.0f + 4.0f * cover) * u::m;
      const meters_t radius =
        (site.form == ForestForm::conifer ? 0.23f : 0.26f) * height;
      append_trunk (
        draw, site, up, across, forward, 0.48f * height, 0.035f * height);
      if (site.form == ForestForm::conifer)
        append_conifer_crown (draw, site, up, across, forward, height, radius);
      else
        append_broadleaf_crown (
          draw, site, up, across, forward, height, radius);
    }

    void append_distant_tree (render::DrawList& draw, const ForestSite& site) {
      const Vec3 surface_normal = site.normal.numerical_value_in (one);
      const Vec3 up =
        normalized (Vec3 (0, 1, 0) * 0.84f + surface_normal * 0.16f);
      const float heading =
        2.0f * std::numbers::pi_v<float> * hash_lane (site.seed, 3);
      Vec3 forward (std::sin (heading), 0.0f, std::cos (heading));
      forward = normalized (forward - up * dot (forward, up));
      const Vec3 across = normalized (cross (up, forward));
      const Vec3& root = position_value (site.position);
      const float cover = site.cover.numerical_value_in (one);
      const float size = site.size.numerical_value_in (one);
      const meters_t height = size * (9.0f + 4.0f * cover) * u::m;
      const meters_t radius =
        (site.form == ForestForm::conifer ? 0.23f : 0.26f) * height;
      const float height_m = height.numerical_value_in (u::m);
      const float radius_m = radius.numerical_value_in (u::m);
      const int sides = 4;
      const float hue =
        hash_lane (site.seed, site.form == ForestForm::conifer ? 23 : 19);
      if (site.form == ForestForm::conifer)
        draw.color (0.045f + 0.025f * hue,
                    0.15f + 0.085f * cover + 0.030f * hue,
                    0.035f + 0.025f * hue);
      else
        draw.color (0.065f + 0.040f * hue,
                    0.19f + 0.10f * cover + 0.040f * hue,
                    0.035f + 0.030f * hue);

      const Vec3 base =
        root + up * (site.form == ForestForm::conifer ? 0.12f * height_m
                                                      : 0.27f * height_m);
      const Vec3 tip = root + up * height_m;
      const Vec3 middle =
        root + up * (site.form == ForestForm::conifer ? 0.22f * height_m
                                                      : 0.61f * height_m);
      Vec3 ring[sides];
      for (int side = 0; side < sides; ++side) {
        const float angle =
          2.0f * std::numbers::pi_v<float> * side / sides + heading;
        ring[side] =
          middle +
          (across * std::cos (angle) + forward * std::sin (angle)) * radius_m;
      }

      constexpr Sway fixed { 0.18f * proportion[one], 0.0f * proportion[one] };
      constexpr Sway crown { 0.72f * proportion[one], 0.68f * proportion[one] };
      constexpr Sway leader { 0.92f * proportion[one],
                              0.12f * proportion[one] };
      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const Vec3& a = ring[side];
        const Vec3& b = ring[(side + 1) % sides];
        const Vec3 outside = (a + b) * 0.5f - middle;
        if (site.form == ForestForm::broadleaf)
          append_triangle (draw, base, b, a, outside, fixed, crown, crown);
        append_triangle (draw, tip, a, b, outside, leader, crown, crown);
      }
      draw.end ();
    }
  }

  ForestPlan plan_global_forest (const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 std::uint32_t seed,
                                 meters_t spacing) {
    if (spacing <= 0.0f * u::m)
      throw std::invalid_argument ("Forest spacing must be positive");
    const terrain::TerrainDomain& domain = surface.domain ();
    ForestPlan plan;
    const meters_t width = domain.period_x ();
    const meters_t depth = domain.period_z ();
    plan.period = spatial_extent_in_metres (Vec3 (
      width.numerical_value_in (u::m), 0, depth.numerical_value_in (u::m)));
    const std::uint32_t columns =
      std::max (1U,
                static_cast<std::uint32_t> (
                  std::ceil ((width / spacing).numerical_value_in (one))));
    const std::uint32_t rows =
      std::max (1U,
                static_cast<std::uint32_t> (
                  std::ceil ((depth / spacing).numerical_value_in (one))));
    const meters_t cell_x = width / static_cast<float> (columns);
    const meters_t cell_z = depth / static_cast<float> (rows);
    plan.sites.reserve (static_cast<std::size_t> (columns) * rows / 4);

    for (std::uint32_t row = 0; row < rows; ++row)
      for (std::uint32_t column = 0; column < columns; ++column) {
        const std::uint32_t identity = forest_hash (column, row, seed);
        const meters_t x = (static_cast<float> (column) + 0.12f +
                            0.76f * hash_lane (identity, 0)) *
                           cell_x;
        const meters_t z =
          (static_cast<float> (row) + 0.12f + 0.76f * hash_lane (identity, 1)) *
          cell_z;
        const map::ForestCover cover = cover_at (readings, x, z);
        const proportion_t population = band (0.08f * map::forest_cover[one],
                                              0.62f * map::forest_cover[one],
                                              cover);
        if (cover < 0.06f * map::forest_cover[one] ||
            hash_lane (identity, 2) >
              population.numerical_value_in (one) * 0.96f)
          continue;
        const terrain::SurfaceElevation elevation =
          elevation_at (surface, x, z);
        const proportion_t high_ground =
          band (terrain::surface_elevation_point (115.0f * u::m),
                terrain::surface_elevation_point (195.0f * u::m),
                elevation);
        const float conifer_chance =
          0.12f + 0.58f * high_ground.numerical_value_in (one);
        plan.sites.push_back (
          { .position = forest_position (x, elevation, z),
            .normal = normal_at (surface, x, z),
            .cover = cover,
            .size =
              (0.78f + 0.50f * hash_lane (identity, 4)) * tree_size_factor[one],
            .seed = identity,
            .form = hash_lane (identity, 5) < conifer_chance
                      ? ForestForm::conifer
                      : ForestForm::broadleaf });
      }
    return plan;
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::rebuild");
    const ForestPlan plan = plan_global_forest (surface, readings, seed);
    m_period = plan.period;
    m_tree_count = plan.sites.size ();
    m_chunks_x = forest_chunks_per_side;
    m_chunks_z = forest_chunks_per_side;
    const meters_t chunk_width =
      extent_component (m_period, 0) / static_cast<float> (m_chunks_x);
    const meters_t chunk_depth =
      extent_component (m_period, 2) / static_cast<float> (m_chunks_z);
    std::vector<std::vector<const ForestSite*>> buckets (
      static_cast<std::size_t> (m_chunks_x) * m_chunks_z);
    for (const ForestSite& site : plan.sites) {
      const int x = std::clamp (
        static_cast<int> ((position_component (site.position, 0) / chunk_width)
                            .numerical_value_in (one)),
        0,
        m_chunks_x - 1);
      const int z = std::clamp (
        static_cast<int> ((position_component (site.position, 2) / chunk_depth)
                            .numerical_value_in (one)),
        0,
        m_chunks_z - 1);
      buckets[static_cast<std::size_t> (z) * m_chunks_x + x].push_back (&site);
    }

    m_chunks.clear ();
    m_chunks.reserve (buckets.size ());
    for (int z = 0; z < m_chunks_z; ++z)
      for (int x = 0; x < m_chunks_x; ++x) {
        const auto& sites =
          buckets[static_cast<std::size_t> (z) * m_chunks_x + x];
        if (sites.empty ())
          continue;
        render::DrawList detailed;
        render::DrawList distant;
        detailed.state ().cull = false;
        distant.state ().cull = false;
        for (const ForestSite* site : sites) {
          append_tree (detailed, *site);
          append_distant_tree (distant, *site);
        }
        Chunk chunk;
        chunk.center = position (
          Vec3 (((x + 0.5f) * chunk_width).numerical_value_in (u::m),
                110.0f,
                ((z + 0.5f) * chunk_depth).numerical_value_in (u::m)));
        chunk.radius = 0.5f * mp_units::sqrt (chunk_width * chunk_width +
                                              chunk_depth * chunk_depth) +
                       190.0f * u::m;
        chunk.detailed_mesh = renderer.create_mesh (detailed);
        chunk.distant_mesh = renderer.create_mesh (distant);
        m_chunks.push_back (std::move (chunk));
      }
  }

  void ForestLandscape::draw (render::Renderer& renderer,
                              const ForestView& view) const {
    const meters_t period_x = extent_component (m_period, 0);
    const meters_t period_z = extent_component (m_period, 2);
    if (period_x <= 0.0f * u::m || period_z <= 0.0f * u::m)
      return;
    for (const Chunk& chunk : m_chunks) {
      const meters_t reach = forest_geometry_reach + chunk.radius;
      const int minimum_x = static_cast<int> (
        std::ceil (((position_component (view.position, 0) - reach -
                     position_component (chunk.center, 0)) /
                    period_x)
                     .numerical_value_in (one)));
      const int maximum_x = static_cast<int> (
        std::floor (((position_component (view.position, 0) + reach -
                      position_component (chunk.center, 0)) /
                     period_x)
                      .numerical_value_in (one)));
      const int minimum_z = static_cast<int> (
        std::ceil (((position_component (view.position, 2) - reach -
                     position_component (chunk.center, 2)) /
                    period_z)
                     .numerical_value_in (one)));
      const int maximum_z = static_cast<int> (
        std::floor (((position_component (view.position, 2) + reach -
                      position_component (chunk.center, 2)) /
                     period_z)
                      .numerical_value_in (one)));
      for (int tile_z = minimum_z; tile_z <= maximum_z; ++tile_z)
        for (int tile_x = minimum_x; tile_x <= maximum_x; ++tile_x) {
          const displacement_t offset =
            displacement (Vec3 ((tile_x * period_x).numerical_value_in (u::m),
                                0,
                                (tile_z * period_z).numerical_value_in (u::m)));
          const displacement_t delta = chunk.center + offset - view.position;
          const auto distance_squared = dot (delta, delta);
          if (distance_squared > reach * reach)
            continue;
          if (!visible_in_view (delta, chunk.radius, view))
            continue;
          const meters_t detailed_reach = forest_detail_reach + chunk.radius;
          const render::MeshPtr& mesh =
            distance_squared <= detailed_reach * detailed_reach
              ? chunk.detailed_mesh
              : chunk.distant_mesh;
          renderer.draw_mesh (*mesh,
                              Mat4::translation (displacement_value (offset)));
        }
    }
  }
}
