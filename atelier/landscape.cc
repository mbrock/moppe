#include "atelier/landscape.hh"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    constexpr Length major_radius = 4.5f * m;
    constexpr Length minor_radius = 1.7f * m;
    constexpr Length puck_radius = 0.39f * m;
    constexpr Length shell_hover = 0.06f * m;
    constexpr Duration fixed_step = (1.0f / 120.0f) * s;
    constexpr Duration cycle_duration = 12.0f * s;
    constexpr Duration split_at = 2.0f * s;
    constexpr Duration split_ends = 4.0f * s;
    constexpr Duration merge_at = 8.0f * s;
    constexpr Duration merge_ends = 10.0f * s;

    constexpr std::array<std::array<int, 2>, 6> even_neighbours {
      std::array { -1, 0 }, std::array { 0, -1 }, std::array { -1, -1 },
      std::array { 1, 0 },  std::array { -1, 1 }, std::array { 0, 1 },
    };
    constexpr std::array<std::array<int, 2>, 6> odd_neighbours {
      std::array { -1, 0 }, std::array { 1, -1 }, std::array { 0, -1 },
      std::array { 1, 0 },  std::array { 0, 1 },  std::array { 1, 1 },
    };

    constexpr std::size_t wrap (int value, std::size_t extent) {
      const int signed_extent = static_cast<int> (extent);
      const int remainder = value % signed_extent;
      return static_cast<std::size_t> (remainder < 0 ? remainder + signed_extent
                                                     : remainder);
    }

    constexpr TileId tile_id (std::size_t column, std::size_t row) {
      return row * landscape_columns + column;
    }

    constexpr TileId neighbour_of (GridCell cell, std::size_t side) {
      const auto& offsets =
        cell.row % 2 == 0 ? even_neighbours : odd_neighbours;
      return tile_id (
        wrap (static_cast<int> (cell.column) + offsets[side][0],
              landscape_columns),
        wrap (static_cast<int> (cell.row) + offsets[side][1], landscape_rows));
    }

    Real smooth (Real value) {
      const Real t = std::clamp (value, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    struct TorusFrame {
      simd_float3 centre;
      simd_float3 across;
      simd_float3 outward;
      simd_float3 around;
    };

    TorusFrame torus_frame (GridCell cell, Length displacement) {
      const Real row_offset = cell.row % 2 == 0 ? 0.0f : 0.5f;
      const Real u = 2.0f * std::numbers::pi_v<Real> *
                     (Real (cell.column) + row_offset) /
                     Real (landscape_columns);
      const Real v = 2.0f * std::numbers::pi_v<Real> * Real (cell.row) /
                     Real (landscape_rows);
      const Real cu = std::cos (u), su = std::sin (u);
      const Real cv = std::cos (v), sv = std::sin (v);
      const simd_float3 outward { cv * cu, sv, cv * su };
      const simd_float3 across { -su, 0, cu };
      const simd_float3 around { -sv * cu, cv, -sv * su };
      const Real major = in_metres (major_radius);
      const Real minor = in_metres (minor_radius);
      const Real lift = in_metres (shell_hover + displacement);
      return {
        .centre = simd_float3 { (major + minor * cv) * cu,
                                minor * sv,
                                (major + minor * cv) * su } +
                  outward * lift,
        .across = across,
        .outward = outward,
        .around = around,
      };
    }

    Matrix
    placement (const TorusFrame& frame, simd_float2 offset, Length radius) {
      const simd_float3 centre =
        frame.centre + frame.across * offset.x + frame.around * offset.y;
      const Real scale = in_metres (radius);
      return {
        simd_make_float4 (frame.across * scale, 0),
        simd_make_float4 (frame.outward * scale, 0),
        simd_make_float4 (frame.around * scale, 0),
        simd_make_float4 (centre, 1),
      };
    }

    Real
    stress_of (const std::array<LandscapeTile, landscape_tile_count>& tiles,
               TileId id) {
      const LandscapeTile& tile = tiles[id];
      Real stress = 0;
      for (TileId neighbour : tile.neighbours)
        stress += std::abs (
          in_metres (tiles[neighbour].displacement - tile.displacement));
      return std::min (1.0f, stress / 0.45f);
    }
  }

  Landscape::Landscape () {
    for (std::size_t row = 0; row < landscape_rows; ++row) {
      for (std::size_t column = 0; column < landscape_columns; ++column) {
        const GridCell cell { column, row };
        const TileId id = tile_id (column, row);
        m_tiles[id] = {
          .cell = cell,
          .neighbours = {
            neighbour_of (cell, 0), neighbour_of (cell, 1),
            neighbour_of (cell, 2), neighbour_of (cell, 3),
            neighbour_of (cell, 4), neighbour_of (cell, 5),
          },
          .displacement = 0.0f * m,
          .velocity = 0.0f * m / s,
        };
      }
    }
    if (!topology_is_valid ())
      throw std::runtime_error ("The toroidal tile topology is inconsistent");
  }

  bool Landscape::topology_is_valid () const {
    for (TileId id = 0; id < m_tiles.size (); ++id) {
      const LandscapeTile& tile = m_tiles[id];
      for (TileId neighbour : tile.neighbours) {
        if (neighbour >= m_tiles.size () || neighbour == id)
          return false;
        const auto& back = m_tiles[neighbour].neighbours;
        if (std::ranges::find (back, id) == back.end ())
          return false;
      }
    }
    return true;
  }

  void Landscape::begin_refinement () {
    const TileId focus = tile_id (landscape_columns / 5, landscape_rows / 2);
    m_tiles[focus].velocity += 1.4f * m / s;
    for (TileId neighbour : m_tiles[focus].neighbours)
      m_tiles[neighbour].velocity += 0.35f * m / s;
  }

  void Landscape::advance (Duration elapsed) {
    if (elapsed < m_elapsed) {
      *this = Landscape ();
    }
    m_accumulator += elapsed - m_elapsed;
    m_elapsed = elapsed;
    while (m_accumulator >= fixed_step) {
      m_simulated += fixed_step;
      update_development (m_simulated);
      step (fixed_step);
      m_accumulator -= fixed_step;
    }
  }

  void Landscape::update_development (Duration elapsed) {
    const Real seconds = elapsed.numerical_value_in (s);
    const Real cycle_seconds = cycle_duration.numerical_value_in (s);
    const std::size_t cycle =
      static_cast<std::size_t> (seconds / cycle_seconds);
    const Duration phase = (seconds - Real (cycle) * cycle_seconds) * s;
    const bool refined = phase >= split_at && phase < merge_ends;
    if (refined && (!m_refined || cycle != m_cycle))
      begin_refinement ();
    m_cycle = cycle;
    m_refined = refined;

    if (phase < split_at || phase >= merge_ends)
      m_refinement = 0;
    else if (phase < split_ends)
      m_refinement =
        smooth (Real ((phase - split_at) / (split_ends - split_at)));
    else if (phase < merge_at)
      m_refinement = 1;
    else
      m_refinement =
        1.0f - smooth (Real ((phase - merge_at) / (merge_ends - merge_at)));
  }

  void Landscape::step (Duration dt) {
    constexpr auto stiffness = 2.3f / (s * s);
    constexpr auto coupling = 5.2f / (s * s);
    constexpr auto damping = 1.25f / s;
    for (TileId id = 0; id < m_tiles.size (); ++id) {
      const LandscapeTile& tile = m_tiles[id];
      Length laplacian {};
      for (TileId neighbour : tile.neighbours)
        laplacian += m_tiles[neighbour].displacement - tile.displacement;
      const auto acceleration = coupling * laplacian -
                                stiffness * tile.displacement -
                                damping * tile.velocity;
      m_next_velocity[id] = tile.velocity + acceleration * dt;
      m_next_displacement[id] = tile.displacement + m_next_velocity[id] * dt;
    }
    for (TileId id = 0; id < m_tiles.size (); ++id) {
      m_tiles[id].velocity = m_next_velocity[id];
      m_tiles[id].displacement = m_next_displacement[id];
    }
  }

  bool Landscape::is_refined () const {
    return m_refined;
  }

  Real Landscape::refinement () const {
    return m_refinement;
  }

  std::vector<TileVisual> Landscape::visuals () const {
    std::vector<TileVisual> result;
    result.reserve (landscape_tile_count + 7);
    const TileId focus = tile_id (landscape_columns / 5, landscape_rows / 2);

    for (TileId id = 0; id < m_tiles.size (); ++id) {
      const LandscapeTile& tile = m_tiles[id];
      const TorusFrame frame = torus_frame (tile.cell, tile.displacement);
      const Real stress = stress_of (m_tiles, id);
      if (id != focus || !m_refined) {
        result.push_back ({ placement (frame, {}, puck_radius), stress, 0 });
        continue;
      }

      const Length parent_radius = (1.0f - m_refinement) * puck_radius;
      if (parent_radius > 0.01f * m)
        result.push_back ({ placement (frame, {}, parent_radius), stress, 0 });

      const Length child_radius =
        std::max (0.01f, m_refinement / 3.0f) * puck_radius;
      result.push_back ({ placement (frame, {}, child_radius), stress, 1 });
      const Real orbit = in_metres (puck_radius / std::numbers::sqrt3_v<Real>);
      for (std::size_t side = 0; side < 6; ++side) {
        const Real angle = Real (side) * 2.0f * std::numbers::pi_v<Real> / 6;
        const simd_float2 offset { orbit * std::sin (angle),
                                   orbit * std::cos (angle) };
        result.push_back (
          { placement (frame, offset, child_radius), stress, 1 });
      }
    }
    return result;
  }
}
