#include "atelier/landscape.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
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

    Real smooth (Real value) {
      const Real t = std::clamp (value, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    Real material_seed (TileId id, std::size_t variant) {
      std::uint32_t bits =
        static_cast<std::uint32_t> (id * 8 + variant + 0x9e3779b9U);
      bits ^= bits >> 16;
      bits *= 0x7feb352dU;
      bits ^= bits >> 15;
      bits *= 0x846ca68bU;
      bits ^= bits >> 16;
      return Real (bits & 0x00ffffffU) / Real (0x01000000U);
    }

    DeformationReading
    deformation_of (const PeriodicHexTopology& topology,
                    const std::vector<NormalDisplacement>& displacement,
                    TileId id) {
      NormalDisplacement total_difference {};
      for (TileId neighbour : topology.neighbours (id)) {
        const NormalDisplacement difference =
          displacement[neighbour] - displacement[id];
        total_difference += difference < 0.0f * m ? -difference : difference;
      }
      const Real reading =
        std::min (1.0f, total_difference.numerical_value_in (m) / 0.45f);
      return DeformationReading (reading * mp_units::one);
    }
  }

  GridCell PeriodicHexTopology::cell (TileId id) const {
    return { id % landscape_columns, id / landscape_columns };
  }

  TileId PeriodicHexTopology::tile_id (GridCell cell) const {
    return cell.row * landscape_columns + cell.column;
  }

  TileId PeriodicHexTopology::neighbour (TileId id, std::size_t side) const {
    const GridCell from = cell (id);
    const auto& offsets = from.row % 2 == 0 ? even_neighbours : odd_neighbours;
    return tile_id ({
      wrap (static_cast<int> (from.column) + offsets[side][0],
            landscape_columns),
      wrap (static_cast<int> (from.row) + offsets[side][1], landscape_rows),
    });
  }

  std::array<TileId, 6> PeriodicHexTopology::neighbours (TileId id) const {
    return { neighbour (id, 0), neighbour (id, 1), neighbour (id, 2),
             neighbour (id, 3), neighbour (id, 4), neighbour (id, 5) };
  }

  bool PeriodicHexTopology::is_valid () const {
    for (TileId id = 0; id < landscape_tile_count; ++id) {
      for (TileId adjacent : neighbours (id)) {
        if (adjacent >= landscape_tile_count || adjacent == id)
          return false;
        const auto reverse = neighbours (adjacent);
        if (std::ranges::find (reverse, id) == reverse.end ())
          return false;
      }
    }
    return true;
  }

  Landscape::Landscape () {
    if (!topology_is_valid ())
      throw std::runtime_error ("The toroidal tile topology is inconsistent");
  }

  bool Landscape::topology_is_valid () const {
    return topology ().is_valid ();
  }

  void Landscape::begin_refinement () {
    auto& velocity = get<normal_velocity> (m_tiles);
    const TileId focus =
      topology ().tile_id ({ landscape_columns / 5, landscape_rows / 2 });
    velocity[focus] += 1.4f * m / s;
    for (TileId neighbour : topology ().neighbours (focus))
      velocity[neighbour] += 0.35f * m / s;
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
    extend_into (m_next_tiles, m_tiles, [=] (const auto& tile) {
      const auto displacement = get<normal_displacement> (tile);
      const auto velocity = get<normal_velocity> (tile);
      const auto acceleration =
        coupling * laplacian<normal_displacement> (tile) -
        stiffness * displacement - damping * velocity;
      const auto next_velocity = velocity + acceleration * dt;
      return bundle_values (displacement + next_velocity * dt, next_velocity);
    });

    auto& [displacement, velocity] = m_tiles;
    auto& [next_displacement, next_velocity] = m_next_tiles;
    velocity.swap (next_velocity);
    displacement.swap (next_displacement);
  }

  bool Landscape::is_refined () const {
    return m_refined;
  }

  Real Landscape::refinement () const {
    return m_refinement;
  }

  std::vector<TileLeaf> Landscape::leaves () const {
    std::vector<TileLeaf> result;
    result.reserve (landscape_tile_count + 7);
    const TileId focus =
      topology ().tile_id ({ landscape_columns / 5, landscape_rows / 2 });
    const auto& displacement = get<normal_displacement> (m_tiles);

    for (TileId id = 0; id < landscape_tile_count; ++id) {
      const GridCell cell = topology ().cell (id);
      const DeformationReading deformation =
        deformation_of (topology (), displacement, id);
      if (id != focus || !m_refined) {
        result.push_back ({ cell,
                            0.0f * m,
                            0.0f * m,
                            puck_radius,
                            displacement[id],
                            deformation,
                            0,
                            material_seed (id, 0) });
        continue;
      }

      const Length parent_radius = (1.0f - m_refinement) * puck_radius;
      if (parent_radius > 0.01f * m)
        result.push_back ({ cell,
                            0.0f * m,
                            0.0f * m,
                            parent_radius,
                            displacement[id],
                            deformation,
                            0,
                            material_seed (id, 0) });

      const Length child_radius =
        std::max (0.01f, m_refinement / 3.0f) * puck_radius;
      result.push_back ({ cell,
                          0.0f * m,
                          0.0f * m,
                          child_radius,
                          displacement[id],
                          deformation,
                          1,
                          material_seed (id, 1) });
      const Length orbit = puck_radius / std::numbers::sqrt3_v<Real>;
      for (std::size_t side = 0; side < 6; ++side) {
        const Real angle = Real (side) * 2.0f * std::numbers::pi_v<Real> / 6;
        result.push_back ({ cell,
                            orbit * std::sin (angle),
                            orbit * std::cos (angle),
                            child_radius,
                            displacement[id],
                            deformation,
                            1,
                            material_seed (id, side + 2) });
      }
    }
    return result;
  }
}
