#include <moppe/game/waterfall_surface.hh>

#include <moppe/terrain/river.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <array>
#include <cmath>

namespace moppe::game {
  namespace {
    using span_coordinate_t = quantity<span_coordinate[one], float>;

    constexpr std::array<span_coordinate_t, 7> cross_positions = {
      -1.0f * span_coordinate[one],  -0.68f * span_coordinate[one],
      -0.34f * span_coordinate[one], 0.0f * span_coordinate[one],
      0.34f * span_coordinate[one],  0.68f * span_coordinate[one],
      1.0f * span_coordinate[one]
    };
    constexpr std::array<proportion_t, 7> cross_opacity = {
      0.0f * proportion[one], 0.72f * proportion[one], 0.94f * proportion[one],
      1.0f * proportion[one], 0.94f * proportion[one], 0.72f * proportion[one],
      0.0f * proportion[one]
    };
    constexpr int vertical_sections = 7;

    position_t cell_position (terrain::CellIndex cell,
                              const map::SurfaceGeometry& surface) {
      const auto& domain = surface.domain ();
      const std::size_t x = cell.value % domain.width ();
      const std::size_t z = cell.value / domain.width ();
      const plan_position_t at =
        plan_position (static_cast<float> (x) * domain.spacing_x (),
                       static_cast<float> (z) * domain.spacing_z ());
      return terrain::at_elevation (
        at,
        spatial::sample<terrain::surface_elevation> (
          surface, at_height (at, 0.0f * u::m)));
    }

    struct CurtainVertex {
      position_t position;
      direction_t normal;
      proportion_t u;     // across, from one edge to the other
      meters_t v;         // down the fall
      proportion_t depth; // the river's, over 2.5 m
      proportion_t opacity;
    };

    void emit (render::DrawList& draw, const CurtainVertex& vertex) {
      draw.color (1.0f,
                  vertex.depth.numerical_value_in (one),
                  1.0f,
                  vertex.opacity.numerical_value_in (one));
      draw.normal (direction_value (vertex.normal));
      draw.uv (vertex.u.numerical_value_in (one), meters_value (vertex.v));
      draw.vertex (position_value (vertex.position));
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

    const plan_extent_t period = plan_extent (surface.domain ().period_x (),
                                              surface.domain ().period_z ());
    draw.begin (render::Prim::Triangles);
    for (const terrain::Waterfall& waterfall : rivers.waterfalls) {
      position_t lip = cell_position (waterfall.lip_cell, surface);
      position_t foot = terrain::nearest_image (
        cell_position (waterfall.foot_cell, surface), lip, period);
      const Vec3& fall = displacement_value (foot - lip);
      // The fall seen from above.
      const displacement_t level_fall =
        displacement (Vec3 (fall[0], 0.0f, fall[2]));
      const meters_t run = length (displacement_value (level_fall)) * u::m;
      if (run < 1e-4f * u::m)
        continue;
      const direction_t downstream =
        unit_direction (displacement_value (level_fall) / meters_value (run));
      const Vec3& d = direction_value (downstream);
      const direction_t across = unit_direction (Vec3 (-d[2], 0.0f, d[0]));
      // A waterfall pinches at its rock lip. Discharge width remains the
      // upper bound, while the selected nickpoint's physical run prevents a
      // steep one-cell fall from becoming a valley-wide translucent board.
      const meters_t width =
        std::min (terrain::river_width (waterfall.contributing_area),
                  std::max (1.5f * u::m, 2.4f * run));
      const meters_t depth =
        0.72f * terrain::river_depth (waterfall.contributing_area);
      position_value (lip)[1] += meters_value (std::max (0.08f * u::m, depth));
      position_value (foot)[1] +=
        meters_value (std::max (0.08f * u::m, 1.15f * depth));
      const meters_t lip_y =
        static_cast<float> (position_value (lip)[1]) * u::m;
      const meters_t foot_y =
        static_cast<float> (position_value (foot)[1]) * u::m;

      std::array<std::array<CurtainVertex, cross_positions.size ()>,
                 vertical_sections>
        rows;
      for (int row = 0; row < vertical_sections; ++row) {
        const blend_weight_t t = static_cast<float> (row) /
                                 static_cast<float> (vertical_sections - 1) *
                                 blend_weight[one];
        // Ballistic fall: horizontal motion is even while gravity makes most
        // of the vertical drop happen late. The widening sheet lands as a
        // broad veil instead of a rigid rectangular board.
        const blend_weight_t fall_t = quantity_cast<blend_weight> (t * t);
        position_t center = lip + quantity_cast<isq::position_vector> (
                                    along (downstream, meters_t (run * t)));
        position_value (center)[1] =
          std::lerp (meters_value (lip_y),
                     meters_value (foot_y),
                     fall_t.numerical_value_in (one));
        const meters_t local_width =
          width * std::lerp (1.0f, 1.22f, t.numerical_value_in (one));
        const displacement_t along_fall = displacement (
          Vec3 (d[0] * meters_value (run),
                meters_value (meters_t (2.0f * t * (foot_y - lip_y))),
                d[2] * meters_value (run)));
        const direction_t normal = unit_direction (normalized (
          cross (direction_value (across), displacement_value (along_fall))));
        for (std::size_t column = 0; column < cross_positions.size ();
             ++column) {
          const span_coordinate_t cross = cross_positions[column];
          rows[row][column] = {
            .position =
              center + quantity_cast<isq::position_vector> (
                         along (across, meters_t (0.5f * local_width * cross))),
            .normal = normal,
            .u = (0.5f * (cross + 1.0f * span_coordinate[one]))
                   .numerical_value_in (one) *
                 proportion[one],
            .v = meters_t (t * hypot (run, foot_y - lip_y)),
            .depth = std::clamp (proportion_t (depth / (2.5f * u::m)),
                                 0.06f * proportion[one],
                                 1.0f * proportion[one]),
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
    m_period = plan_extent (surface.domain ().period_x (),
                            surface.domain ().period_z ());
  }

  void WaterfallSurface::clear () {
    m_mesh.reset ();
  }

  void WaterfallSurface::draw (render::Renderer& renderer,
                               const position_t& camera) const {
    if (!m_mesh)
      return;
    const plan_position_t eye = plan (camera);
    const meters_t period_x = plan_value (m_period).x () * u::m;
    const meters_t period_z = plan_value (m_period).z () * u::m;
    const meters_t base_x =
      std::floor ((x_of (eye) / period_x).numerical_value_in (one)) * period_x;
    const meters_t base_z =
      std::floor ((z_of (eye) / period_z).numerical_value_in (one)) * period_z;
    for (int z = -1; z <= 1; ++z)
      for (int x = -1; x <= 1; ++x)
        renderer.draw_waterfalls (
          *m_mesh,
          Mat4::translation (
            Vec3 (meters_value (base_x + static_cast<float> (x) * period_x),
                  0.0f,
                  meters_value (base_z + static_cast<float> (z) * period_z))));
  }
}
