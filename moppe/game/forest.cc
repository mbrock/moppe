#include <moppe/game/forest.hh>

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
    constexpr float forest_geometry_reach = 2200.0f;

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

    float smoothstep (float edge0, float edge1, float value) {
      const float t =
        std::clamp ((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    float elevation_at (const map::Surface& surface, float x, float z) {
      return surface.elevation_at (position (Vec3 (x, 0, z)))
        .quantity_from_zero ()
        .numerical_value_in (u::m);
    }

    Vec3 normal_at (const map::Surface& surface, float x, float z) {
      return normalized (
        surface.normal_at (position (Vec3 (x, 0, z))).numerical_value_in (one));
    }

    float cover_at (const map::Surface& surface, float x, float z) {
      return surface.forest_cover_at (position (Vec3 (x, 0, z)))
        .numerical_value_in (one);
    }

    void forest_vertex (render::DrawList& draw,
                        const Vec3& point,
                        const Vec3& normal,
                        float wind) {
      draw.normal (normal);
      draw.wind (std::clamp (wind, 0.0f, 1.0f) * proportion[one]);
      draw.vertex (point);
    }

    void append_triangle (render::DrawList& draw,
                          Vec3 a,
                          Vec3 b,
                          Vec3 c,
                          const Vec3& outside,
                          float wind_a,
                          float wind_b,
                          float wind_c) {
      Vec3 normal = normalized (cross (b - a, c - a));
      if (dot (normal, outside) < 0.0f) {
        std::swap (b, c);
        std::swap (wind_b, wind_c);
        normal = -normal;
      }
      forest_vertex (draw, a, normal, wind_a);
      forest_vertex (draw, b, normal, wind_b);
      forest_vertex (draw, c, normal, wind_c);
    }

    void append_trunk (render::DrawList& draw,
                       const ForestSite& site,
                       const Vec3& up,
                       const Vec3& across,
                       const Vec3& forward,
                       float height,
                       float radius) {
      const float tint = hash_lane (site.seed, 11);
      draw.color (
        0.16f + 0.040f * tint, 0.075f + 0.028f * tint, 0.028f + 0.016f * tint);
      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < 3; ++side) {
        const float a0 = 2.0f * std::numbers::pi_v<float> * side / 3.0f;
        const float a1 = 2.0f * std::numbers::pi_v<float> * (side + 1) / 3.0f;
        const Vec3 n0 = across * std::cos (a0) + forward * std::sin (a0);
        const Vec3 n1 = across * std::cos (a1) + forward * std::sin (a1);
        const Vec3 p0 = site.position + n0 * radius;
        const Vec3 p1 = site.position + n1 * radius;
        const Vec3 q0 = p0 + up * height;
        const Vec3 q1 = p1 + up * height;
        const Vec3 normal = normalized (n0 + n1);
        forest_vertex (draw, p0, normal, 0.0f);
        forest_vertex (draw, p1, normal, 0.0f);
        forest_vertex (draw, q1, normal, 0.16f);
        forest_vertex (draw, p0, normal, 0.0f);
        forest_vertex (draw, q1, normal, 0.16f);
        forest_vertex (draw, q0, normal, 0.16f);
      }
      draw.end ();
    }

    void append_broadleaf_crown (render::DrawList& draw,
                                 const ForestSite& site,
                                 const Vec3& up,
                                 const Vec3& across,
                                 const Vec3& forward,
                                 float height,
                                 float radius) {
      constexpr int sides = 6;
      const Vec3 bottom = site.position + up * (0.30f * height);
      const Vec3 top = site.position + up * height;
      const Vec3 center = site.position + up * (0.61f * height);
      Vec3 ring[sides];
      for (int side = 0; side < sides; ++side) {
        const float angle = 2.0f * std::numbers::pi_v<float> * side / sides;
        const float irregular =
          0.86f + 0.25f * hash_lane (site.seed, 31 + side);
        ring[side] =
          center +
          (across * std::cos (angle) + forward * std::sin (angle)) * radius *
            irregular +
          up * radius * (0.08f * (hash_lane (site.seed, 47 + side) - 0.5f));
      }

      const float hue = hash_lane (site.seed, 19);
      draw.color (0.065f + 0.040f * hue,
                  0.19f + 0.10f * site.cover + 0.040f * hue,
                  0.035f + 0.030f * hue);
      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const Vec3& a = ring[side];
        const Vec3& b = ring[(side + 1) % sides];
        const Vec3 outside = (a + b) * 0.5f - center;
        append_triangle (draw, bottom, b, a, outside, 0.22f, 0.58f, 0.58f);
        append_triangle (draw, top, a, b, outside, 0.92f, 0.58f, 0.58f);
      }
      draw.end ();
    }

    void append_conifer_crown (render::DrawList& draw,
                               const ForestSite& site,
                               const Vec3& up,
                               const Vec3& across,
                               const Vec3& forward,
                               float height,
                               float radius) {
      constexpr int sides = 7;
      const Vec3 base = site.position + up * (0.24f * height);
      const Vec3 top = site.position + up * (1.06f * height);
      const float hue = hash_lane (site.seed, 23);
      draw.color (0.045f + 0.025f * hue,
                  0.15f + 0.085f * site.cover + 0.030f * hue,
                  0.035f + 0.025f * hue);
      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const float a0 = 2.0f * std::numbers::pi_v<float> * side / sides;
        const float a1 = 2.0f * std::numbers::pi_v<float> * (side + 1) / sides;
        const Vec3 p0 =
          base + (across * std::cos (a0) + forward * std::sin (a0)) * radius;
        const Vec3 p1 =
          base + (across * std::cos (a1) + forward * std::sin (a1)) * radius;
        append_triangle (
          draw, top, p0, p1, (p0 + p1) * 0.5f - base, 0.90f, 0.30f, 0.30f);
      }
      draw.end ();
    }

    void append_tree (render::DrawList& draw, const ForestSite& site) {
      const Vec3 up = normalized (Vec3 (0, 1, 0) * 0.78f + site.normal * 0.22f);
      const float heading =
        2.0f * std::numbers::pi_v<float> * hash_lane (site.seed, 3);
      Vec3 forward (std::sin (heading), 0.0f, std::cos (heading));
      forward = normalized (forward - up * dot (forward, up));
      const Vec3 across = normalized (cross (up, forward));
      const float height = site.scale * (9.0f + 4.0f * site.cover);
      const float radius =
        site.scale *
        (site.form == ForestForm::conifer ? 0.23f * height : 0.26f * height);
      append_trunk (
        draw, site, up, across, forward, 0.48f * height, 0.035f * height);
      if (site.form == ForestForm::conifer)
        append_conifer_crown (draw, site, up, across, forward, height, radius);
      else
        append_broadleaf_crown (
          draw, site, up, across, forward, height, radius);
    }
  }

  ForestPlan plan_global_forest (const map::Surface& surface,
                                 std::uint32_t seed,
                                 float spacing) {
    if (spacing <= 0.0f)
      throw std::invalid_argument ("Forest spacing must be positive");
    const map::SurfaceDomain& domain = surface.samples ().domain ();
    ForestPlan plan;
    plan.periodic = domain.topology () == terrain::Topology::Torus;
    const float width =
      meters_value (domain.spacing_x ()) *
      static_cast<float> (plan.periodic ? domain.width () - 1
                                        : domain.width () - 2);
    const float depth =
      meters_value (domain.spacing_z ()) *
      static_cast<float> (plan.periodic ? domain.height () - 1
                                        : domain.height () - 2);
    plan.period = Vec3 (width, 0, depth);
    const std::uint32_t columns =
      std::max (1U, static_cast<std::uint32_t> (std::ceil (width / spacing)));
    const std::uint32_t rows =
      std::max (1U, static_cast<std::uint32_t> (std::ceil (depth / spacing)));
    const float cell_x = width / static_cast<float> (columns);
    const float cell_z = depth / static_cast<float> (rows);
    plan.sites.reserve (static_cast<std::size_t> (columns) * rows / 4);

    for (std::uint32_t row = 0; row < rows; ++row)
      for (std::uint32_t column = 0; column < columns; ++column) {
        const std::uint32_t identity = forest_hash (column, row, seed);
        const float x = (static_cast<float> (column) + 0.12f +
                         0.76f * hash_lane (identity, 0)) *
                        cell_x;
        const float z =
          (static_cast<float> (row) + 0.12f + 0.76f * hash_lane (identity, 1)) *
          cell_z;
        const float cover = cover_at (surface, x, z);
        const float population = smoothstep (0.08f, 0.62f, cover);
        if (cover < 0.06f || hash_lane (identity, 2) > population * 0.96f)
          continue;
        const float elevation = elevation_at (surface, x, z);
        const float high_ground =
          std::clamp ((elevation - 115.0f) / 80.0f, 0.0f, 1.0f);
        const float conifer_chance = 0.12f + 0.58f * high_ground;
        plan.sites.push_back (
          { .position = Vec3 (x, elevation, z),
            .normal = normal_at (surface, x, z),
            .cover = cover,
            .scale = 0.78f + 0.50f * hash_lane (identity, 4),
            .seed = identity,
            .form = hash_lane (identity, 5) < conifer_chance
                      ? ForestForm::conifer
                      : ForestForm::broadleaf });
      }
    return plan;
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const map::Surface& surface,
                                 std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::rebuild");
    const ForestPlan plan = plan_global_forest (surface, seed);
    m_period = plan.period;
    m_periodic = plan.periodic;
    m_tree_count = plan.sites.size ();
    m_chunks_x = forest_chunks_per_side;
    m_chunks_z = forest_chunks_per_side;
    const float chunk_width = m_period[0] / m_chunks_x;
    const float chunk_depth = m_period[2] / m_chunks_z;
    std::vector<std::vector<const ForestSite*>> buckets (
      static_cast<std::size_t> (m_chunks_x) * m_chunks_z);
    for (const ForestSite& site : plan.sites) {
      const int x = std::clamp (
        static_cast<int> (site.position[0] / chunk_width), 0, m_chunks_x - 1);
      const int z = std::clamp (
        static_cast<int> (site.position[2] / chunk_depth), 0, m_chunks_z - 1);
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
        render::DrawList draw;
        draw.state ().cull = false;
        for (const ForestSite* site : sites)
          append_tree (draw, *site);
        Chunk chunk;
        chunk.center =
          Vec3 ((x + 0.5f) * chunk_width, 110.0f, (z + 0.5f) * chunk_depth);
        chunk.radius = 0.5f * std::sqrt (chunk_width * chunk_width +
                                         chunk_depth * chunk_depth) +
                       190.0f;
        chunk.mesh = renderer.create_mesh (draw);
        m_chunks.push_back (std::move (chunk));
      }
  }

  void ForestLandscape::draw (render::Renderer& renderer,
                              const Vec3& camera,
                              const Vec3& view_direction) const {
    if (m_period[0] <= 0.0f || m_period[2] <= 0.0f)
      return;
    for (const Chunk& chunk : m_chunks) {
      const float reach = forest_geometry_reach + chunk.radius;
      const int minimum_x =
        m_periodic ? static_cast<int> (std::ceil (
                       (camera[0] - reach - chunk.center[0]) / m_period[0]))
                   : 0;
      const int maximum_x =
        m_periodic ? static_cast<int> (std::floor (
                       (camera[0] + reach - chunk.center[0]) / m_period[0]))
                   : 0;
      const int minimum_z =
        m_periodic ? static_cast<int> (std::ceil (
                       (camera[2] - reach - chunk.center[2]) / m_period[2]))
                   : 0;
      const int maximum_z =
        m_periodic ? static_cast<int> (std::floor (
                       (camera[2] + reach - chunk.center[2]) / m_period[2]))
                   : 0;
      for (int tile_z = minimum_z; tile_z <= maximum_z; ++tile_z)
        for (int tile_x = minimum_x; tile_x <= maximum_x; ++tile_x) {
          const Vec3 offset (tile_x * m_period[0], 0, tile_z * m_period[2]);
          const Vec3 delta = chunk.center + offset - camera;
          const float distance_squared = length2 (delta);
          if (distance_squared > reach * reach)
            continue;
          if (distance_squared > chunk.radius * chunk.radius &&
              dot (delta, view_direction) < -chunk.radius)
            continue;
          renderer.draw_mesh (*chunk.mesh, Mat4::translation (offset));
        }
    }
  }
}
