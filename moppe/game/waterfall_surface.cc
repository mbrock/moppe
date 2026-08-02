#include <moppe/game/waterfall_surface.hh>

#include <moppe/terrain/river.hh>

#include <algorithm>
#include <array>
#include <cmath>

namespace moppe::game {
  namespace {
    constexpr std::array<float, 7> cross_positions = { -1.0f, -0.68f, -0.34f,
                                                       0.0f,  0.34f,  0.68f,
                                                       1.0f };
    constexpr std::array<float, 7> cross_opacity = { 0.0f,  0.72f, 0.94f, 1.0f,
                                                     0.94f, 0.72f, 0.0f };
    constexpr int vertical_sections = 7;

    Vec3 cell_position (terrain::CellIndex cell,
                        const map::SurfaceGeometry& surface) {
      const auto& domain = surface.domain ();
      const std::size_t x = cell.value % domain.width ();
      const std::size_t z = cell.value / domain.width ();
      const float world_x = x * (domain.spacing_x ()).numerical_value_in (u::m);
      const float world_z = z * (domain.spacing_z ()).numerical_value_in (u::m);
      const float ground = terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, position (Vec3 (world_x, 0.0f, world_z))));
      return Vec3 (world_x, ground, world_z);
    }

    Vec3 nearest_image (Vec3 point,
                        const Vec3& reference,
                        const map::SurfaceGeometry& surface) {
      const float period_x =
        (surface.domain ().period_x ()).numerical_value_in (u::m);
      const float period_z =
        (surface.domain ().period_z ()).numerical_value_in (u::m);
      point[0] =
        reference[0] + std::remainder (point[0] - reference[0], period_x);
      point[2] =
        reference[2] + std::remainder (point[2] - reference[2], period_z);
      return point;
    }

    struct CurtainVertex {
      Vec3 position;
      Vec3 normal;
      float u;
      float v;
      float depth;
      float opacity;
    };

    void emit (render::DrawList& draw, const CurtainVertex& vertex) {
      draw.color (1.0f, vertex.depth, 1.0f, vertex.opacity);
      draw.normal (vertex.normal);
      draw.uv (vertex.u, vertex.v);
      draw.vertex (vertex.position);
    }
  }

  render::DrawList
  build_waterfall_curtains (const map::SurfaceGeometry& surface,
                            const terrain::RiverNetwork& rivers) {
    render::DrawList draw;
    render::DrawState state;
    state.blend = true;
    state.depth_write = false;
    state.cull = false;
    draw.state (state);
    draw.lit (false);
    draw.fogged (true);

    draw.begin (render::Prim::Triangles);
    for (const terrain::Waterfall& waterfall : rivers.waterfalls) {
      Vec3 lip = cell_position (waterfall.lip_cell, surface);
      Vec3 foot = nearest_image (
        cell_position (waterfall.foot_cell, surface), lip, surface);
      Vec3 downstream = foot - lip;
      downstream[1] = 0.0f;
      const float run = length (downstream);
      if (run < 1e-4f)
        continue;
      downstream /= run;
      const Vec3 across (-downstream[2], 0.0f, downstream[0]);
      // A waterfall pinches at its rock lip. Discharge width remains the
      // upper bound, while the selected nickpoint's physical run prevents a
      // steep one-cell fall from becoming a valley-wide translucent board.
      const float width =
        std::min (terrain::river_width (waterfall.contributing_area)
                    .numerical_value_in (u::m),
                  std::max (1.5f, 2.4f * run));
      const float depth =
        0.72f * terrain::river_depth (waterfall.contributing_area)
                  .numerical_value_in (u::m);
      lip[1] += std::max (0.08f, depth);
      foot[1] += std::max (0.08f, 1.15f * depth);

      std::array<std::array<CurtainVertex, cross_positions.size ()>,
                 vertical_sections>
        rows;
      for (int row = 0; row < vertical_sections; ++row) {
        const float t =
          static_cast<float> (row) / static_cast<float> (vertical_sections - 1);
        // Ballistic fall: horizontal motion is even while gravity makes most
        // of the vertical drop happen late. The widening sheet lands as a
        // broad veil instead of a rigid rectangular board.
        const float fall_t = t * t;
        Vec3 center = lip + downstream * (run * t);
        center[1] = std::lerp (lip[1], foot[1], fall_t);
        const float local_width = width * std::lerp (1.0f, 1.22f, t);
        const Vec3 along (downstream[0] * run,
                          2.0f * t * (foot[1] - lip[1]),
                          downstream[2] * run);
        const Vec3 normal = normalized (cross (across, along));
        for (std::size_t column = 0; column < cross_positions.size ();
             ++column) {
          const float cross = cross_positions[column];
          rows[row][column] = {
            .position = center + across * (0.5f * local_width * cross),
            .normal = normal,
            .u = 0.5f * (cross + 1.0f),
            .v = t * std::hypot (run, foot[1] - lip[1]),
            .depth = std::clamp (depth / 2.5f, 0.06f, 1.0f),
            .opacity = cross_opacity[column]
          };
        }
      }

      for (int row = 0; row + 1 < vertical_sections; ++row)
        for (std::size_t column = 0; column + 1 < cross_positions.size ();
             ++column) {
          emit (draw, rows[row][column]);
          emit (draw, rows[row + 1][column]);
          emit (draw, rows[row + 1][column + 1]);
          emit (draw, rows[row][column]);
          emit (draw, rows[row + 1][column + 1]);
          emit (draw, rows[row][column + 1]);
        }
    }
    draw.end ();
    return draw;
  }

  void WaterfallSurface::rebuild (render::Renderer& renderer,
                                  const map::SurfaceGeometry& surface,
                                  const terrain::RiverNetwork& rivers) {
    const render::DrawList draw = build_waterfall_curtains (surface, rivers);
    m_mesh = draw.empty () ? nullptr : renderer.create_mesh (draw);
    m_period = Vec3 ((surface.domain ().period_x ()).numerical_value_in (u::m),
                     0.0f,
                     (surface.domain ().period_z ()).numerical_value_in (u::m));
  }

  void WaterfallSurface::clear () {
    m_mesh.reset ();
  }

  void WaterfallSurface::draw (render::Renderer& renderer,
                               const Vec3& camera) const {
    if (!m_mesh)
      return;
    const float base_x = std::floor (camera[0] / m_period[0]) * m_period[0];
    const float base_z = std::floor (camera[2] / m_period[2]) * m_period[2];
    for (int z = -1; z <= 1; ++z)
      for (int x = -1; x <= 1; ++x)
        renderer.draw_waterfalls (
          *m_mesh,
          Mat4::translation (
            Vec3 (base_x + x * m_period[0], 0.0f, base_z + z * m_period[2])));
  }
}
